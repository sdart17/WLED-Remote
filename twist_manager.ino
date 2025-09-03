// ─────────────────────────────────────────────────────────────
// twist_manager.ino - Enhanced with System Power Management
// Qwiic Twist RGB ring driven by WLED palette/effect.
// - Encoder button toggles system power mode (WLED + screen + encoder LED)
// - Prefers "Set Colors" from state
// - Else samples /json/live (throttled)  
// - Else ROYGBIV fallback
// FIXED: Hold steady for 4s, then cross-fade for 1s between colors
// FIXED: Encoder button now properly controls screen state
// FIXED: Always falls back to ROYGBIV when WLED unavailable
// ─────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <SparkFun_Qwiic_Twist_Arduino_Library.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>

// ---------- External config (safe defaults if not provided) ----------
#ifndef HTTP_TIMEOUT_MS
  #define HTTP_TIMEOUT_MS 600
#endif
#ifndef WLED_IP
  #define WLED_IP "wled.local"        // Prefer fixed IP in config.ino
#endif

// FIXED: Timing constants for hold/fade pattern
#define TWIST_COLOR_HOLD_TIME_MS 4000UL    // Hold steady for 4 seconds
#define TWIST_COLOR_FADE_TIME_MS 1000UL    // Cross-fade for 1 second
#define TWIST_TOTAL_CYCLE_TIME_MS (TWIST_COLOR_HOLD_TIME_MS + TWIST_COLOR_FADE_TIME_MS)  // 5s total

// Enable/Disable /json/live sampling (expensive if abused). ON + throttled.
#ifndef TWIST_ENABLE_JSON_LIVE
  #define TWIST_ENABLE_JSON_LIVE 1
#endif
#ifndef TWIST_JSON_LIVE_MIN_INTERVAL_MS
  #define TWIST_JSON_LIVE_MIN_INTERVAL_MS 5000UL   // sample at most every 5s
#endif

// ---------- WLED JSON source color-order remap ----------
#define COLOR_ORDER_RGB 1
#define COLOR_ORDER_GRB 2
#define COLOR_ORDER_BRG 3
#define COLOR_ORDER_BGR 4
#define COLOR_ORDER_RBG 5
#define COLOR_ORDER_GBR 6
#ifndef TWIST_WLED_JSON_COLOR_ORDER
  #define TWIST_WLED_JSON_COLOR_ORDER COLOR_ORDER_RGB
#endif

// ---------- I2C (Qwiic on ESP32-S3 Touch 2.8 uses Wire1) ----------
#define TWIST_I2C_SDA 11
#define TWIST_I2C_SCL 10
#define TWIST_ADDR    0x3F

// ---------- Types ----------
struct TwistRGB { uint8_t r, g, b; };
#define MAKE_RGB(R,G,B) (TwistRGB{ (uint8_t)(R), (uint8_t)(G), (uint8_t)(B) })

// ---------- Power Management Types (define locally to avoid dependency issues) ----------
enum TwistPowerMode_Local {
  TWIST_POWER_NORMAL_LOCAL = 0,     // Normal operation - screen on, encoder LED on
  TWIST_POWER_SAVING_LOCAL = 1,     // Power saving - screen off, encoder LED off, WLED off
  TWIST_POWER_TRANSITION_LOCAL = 2  // Temporary state during mode changes
};

static inline bool isNearRGB(uint8_t ar, uint8_t ag, uint8_t ab,
                             uint8_t br, uint8_t bg, uint8_t bb,
                             uint8_t tol = 10) {
  return (abs((int)ar - (int)br) <= tol) &&
         (abs((int)ag - (int)bg) <= tol) &&
         (abs((int)ab - (int)bb) <= tol);
}

// Map incoming (in0,in1,in2) from WLED JSON to (r,g,b) as true RGB
static inline void fromWledOrder(uint8_t in0, uint8_t in1, uint8_t in2,
                                 uint8_t &r, uint8_t &g, uint8_t &b) {
  #if   TWIST_WLED_JSON_COLOR_ORDER == COLOR_ORDER_RGB
    r = in0; g = in1; b = in2;
  #elif TWIST_WLED_JSON_COLOR_ORDER == COLOR_ORDER_GRB
    r = in1; g = in0; b = in2;
  #elif TWIST_WLED_JSON_COLOR_ORDER == COLOR_ORDER_BRG
    r = in1; g = in2; b = in0;
  #elif TWIST_WLED_JSON_COLOR_ORDER == COLOR_ORDER_BGR
    r = in2; g = in1; b = in0;
  #elif TWIST_WLED_JSON_COLOR_ORDER == COLOR_ORDER_RBG
    r = in0; g = in2; b = in1;
  #elif TWIST_WLED_JSON_COLOR_ORDER == COLOR_ORDER_GBR
    r = in2; g = in0; b = in1;
  #else
    r = in0; g = in1; b = in2;
  #endif
}

// ---------- External functions provided elsewhere ----------
bool WiFiManager_isConnected();
bool WLEDClient_fetchWledState(ArduinoJson::JsonDocument& doc);
bool WLEDClient_sendBrightness(uint8_t bri);
bool WLEDClient_getPowerState();
bool WLEDClient_setPowerState(bool on);
void DisplayManager_updateActivity();
bool DisplayManager_isScreenOn();
void UIManager_forceRepaint();

// ---------- Module state ----------
static TWIST     TwistManager_twist;
static bool      TwistManager_available = false;
static uint32_t TwistManager_lastUpdate = 0;

// ENHANCED: Power management state
static TwistPowerMode_Local TwistManager_powerMode = TWIST_POWER_NORMAL_LOCAL;
static bool TwistManager_wledPowerState = true; // assume ON initially
static uint32_t TwistManager_lastPowerToggle = 0;
static const uint32_t POWER_TOGGLE_DEBOUNCE_MS = 500; // Prevent rapid toggling

// Manual color set (used when not palette-cycling)
static const TwistRGB TwistManager_colors[] = {
  {255,   0,   0}, // red
  {  0, 255,   0}, // green
  {  0,   0, 255}, // blue
  {255, 255, 255}, // white
};
static const uint8_t TWIST_COLOR_COUNT =
  sizeof(TwistManager_colors) / sizeof(TwistManager_colors[0]);
static uint8_t TwistManager_colorIndex = 0;

// Palette buffers
static TwistRGB TwistManager_paletteColors[16];
static uint8_t  TwistManager_paletteColorCount = 0;
static uint8_t  TwistManager_currentPaletteIndex = 0;
static bool     TwistManager_usePaletteCycling = false;

// FIXED: New timing variables for hold/fade pattern
static uint32_t TwistManager_cycleStartTime = 0;
static TwistRGB TwistManager_currentDisplayColor = {128,128,128};
static TwistRGB TwistManager_fromColor = {128,128,128};
static TwistRGB TwistManager_toColor = {128,128,128};

// Brightness
static int  TwistManager_currentBrightness = 128;
static const int TWIST_BRIGHTNESS_STEP = 5;
static const int TWIST_MIN_BRIGHTNESS = 1;
static const int TWIST_MAX_BRIGHTNESS = 255;

#if TWIST_ENABLE_JSON_LIVE
static uint32_t TwistManager_lastLiveSample = 0;
#endif

// ---------- Internal function declarations ----------
static void     TwistManager_updateLED();
static void     TwistManager_updatePaletteAnimation();
static void     TwistManager_advanceToNextColor();
static void     TwistManager_handleRotation(int16_t diff);
static bool     TwistManager_fetchWLEDPowerState();
static void     TwistManager_buildFallbackFromSegment(const JsonObject& seg);
static void     TwistManager_useFallbackROYGBIV();
static uint8_t  lerp8(uint8_t a, uint8_t b, uint8_t t, uint8_t tmax);
static void     TwistManager_enterPowerSavingMode();
static void     TwistManager_exitPowerSavingMode();
static void     TwistManager_handlePowerToggle();
#if TWIST_ENABLE_JSON_LIVE
static void     TwistManager_tryJsonLiveSample();
#endif

// ---------- Public API ----------
void TwistManager_init() {
  Serial.println("[TWIST] Initializing Qwiic Twist…");

  // I2C on Wire1
  Wire1.begin(TWIST_I2C_SDA, TWIST_I2C_SCL);
  Wire1.setClock(400000); // keep qwiic snappy

  if (!TwistManager_twist.begin(Wire1, TWIST_ADDR)) {
    Serial.println("[TWIST] Qwiic Twist NOT found; disabling.");
    TwistManager_available = false;
    return;
  }

  TwistManager_available = true;
  Serial.println("[TWIST] Qwiic Twist online.");

  // Initialize timing
  TwistManager_cycleStartTime = millis();
  TwistManager_powerMode = TWIST_POWER_NORMAL_LOCAL;

  // CRITICAL: Always start with ROYGBIV fallback, then try to sync with WLED
  Serial.println("[TWIST] Starting with ROYGBIV fallback");
  TwistManager_useFallbackROYGBIV();
  
  // Try to sync with WLED (this will keep ROYGBIV if sync fails)
  TwistManager_syncWithWLED();
  TwistManager_updateLED();
  
  Serial.printf("[TWIST] Initialized in %s mode\n", 
                TwistManager_powerMode == TWIST_POWER_NORMAL_LOCAL ? "NORMAL" : "POWER_SAVING");
}

void TwistManager_update() {
  if (!TwistManager_available) return;

  uint32_t now = millis();
  // Use config-defined update interval
  if (now - TwistManager_lastUpdate < TWIST_UPDATE_INTERVAL_MS) return;
  TwistManager_lastUpdate = now;

  // ENHANCED: Handle button press for power management
  TwistManager_handlePowerToggle();
  
  // Periodic palette refresh to keep encoder LED colors in sync
  TwistManager_periodicPaletteRefresh();

  // FIXED: LED should follow screen state - check if screen is on
  bool screenIsOn = DisplayManager_isScreenOn();
  
  if (screenIsOn) {
    // Screen is on - encoder LED should be on (normal mode)
    if (TwistManager_powerMode != TWIST_POWER_NORMAL_LOCAL) {
      Serial.println("[TWIST] Screen detected ON - switching to normal mode");
      TwistManager_powerMode = TWIST_POWER_NORMAL_LOCAL;
    }
    
    // Update palette animation and LED when screen is on
    TwistManager_updatePaletteAnimation();
    TwistManager_updateLED();

    // Handle rotation for brightness control
    int16_t diff = TwistManager_twist.getDiff(); // clears counter
    if (diff != 0) {
      TwistManager_handleRotation(diff);
      DisplayManager_updateActivity(); // Keep screen alive on encoder interaction
    }
  } else {
    // Screen is off - encoder LED should be off (power saving mode)
    if (TwistManager_powerMode != TWIST_POWER_SAVING_LOCAL) {
      Serial.println("[TWIST] Screen detected OFF - switching to power saving mode");
      TwistManager_powerMode = TWIST_POWER_SAVING_LOCAL;
      TwistManager_twist.setColor(0, 0, 0); // Turn off LED immediately
    }
    
    // Still check for encoder activity to wake up
    int16_t diff = TwistManager_twist.getDiff();
    if (diff != 0) {
      Serial.println("[TWIST] Encoder activity while screen off - waking up");
      TwistManager_exitPowerSavingMode();
      DisplayManager_updateActivity();
    }
  }
}

bool TwistManager_isAvailable() { return TwistManager_available; }
int  TwistManager_getCurrentBrightness() { return TwistManager_currentBrightness; }

void TwistManager_setBrightness(int brightness) {
  TwistManager_currentBrightness = constrain(brightness, TWIST_MIN_BRIGHTNESS, TWIST_MAX_BRIGHTNESS);
  TwistManager_updateLED();
}

// PHASE 2A: Optimized sync using cached data instead of separate HTTP requests
void TwistManager_syncWithWLED() {
  if (!TwistManager_available || !WiFiManager_isConnected()) {
    Serial.println("[TWIST] WiFi down or unavailable - using ROYGBIV fallback");
    TwistManager_useFallbackROYGBIV();
    return;
  }

  // PHASE 2A: Use cached brightness from WLEDClient instead of separate HTTP request
  int bri = WLEDClient_fetchBrightness(); // Uses cached value if recent
  if (bri >= 0 && bri != TwistManager_currentBrightness) {
    TwistManager_currentBrightness = bri;
    Serial.printf("[TWIST] Synced brightness: %d (cached)\n", TwistManager_currentBrightness);
  }

  // PHASE 2A: Only fetch palette/power state occasionally to reduce HTTP traffic
  static uint32_t lastFullSync = 0;
  uint32_t now = millis();
  if (now - lastFullSync > 60000) { // Full sync every 60s instead of every call
    // Use cached power state
    bool powerState = WLEDClient_getPowerState();
    Serial.printf("[TWIST] Power state sync: %s (cached)\n", powerState ? "ON" : "OFF");
    
    // Only fetch palette occasionally
    TwistManager_fetchWLEDPalette();
    lastFullSync = now;
  }
}

void TwistManager_onProgramChange() {
  if (!TwistManager_available) return;
  if (!WiFiManager_isConnected()) { 
    Serial.println("[TWIST] WiFi down - using ROYGBIV fallback"); 
    TwistManager_useFallbackROYGBIV(); 
    return; 
  }
  Serial.println("[TWIST] Program change → refresh palette");
  TwistManager_fetchWLEDPalette();
}

// Periodic palette refresh to keep encoder LED colors in sync
void TwistManager_periodicPaletteRefresh() {
  if (!TwistManager_available) return;
  
  static uint32_t lastRefresh = 0;
  uint32_t now = millis();
  
  // Refresh palette every 30 seconds to keep colors in sync
  if (now - lastRefresh > 30000) {
    if (WiFiManager_isConnected()) {
      TwistManager_fetchWLEDPalette();
      lastRefresh = now;
    }
  }
}

// ENHANCED: Power management functions
bool TwistManager_isInPowerSavingMode() {
  return TwistManager_powerMode == TWIST_POWER_SAVING_LOCAL;
}

// ---------- Internal implementations ----------
static void TwistManager_updateLED() {
  if (!TwistManager_available) return;

  // Check power mode - if in power saving, turn off encoder LED
  if (TwistManager_powerMode == TWIST_POWER_SAVING_LOCAL) {
    TwistManager_twist.setColor(0,0,0);
    return;
  }

  // Check WLED power state - if WLED is off but we're not in power saving, show dim color
  if (!TwistManager_wledPowerState) {
    // Show a dim red to indicate WLED is off but system is still active
    TwistManager_twist.setColor(32, 0, 0);
    return;
  }

  // Normal operation - show colors based on current mode
  if (TwistManager_usePaletteCycling) {
    TwistManager_twist.setColor(TwistManager_currentDisplayColor.r,
                                TwistManager_currentDisplayColor.g,
                                TwistManager_currentDisplayColor.b);
  } else {
    const TwistRGB& c = TwistManager_colors[TwistManager_colorIndex];
    TwistManager_twist.setColor(c.r, c.g, c.b);
  }
}

static uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t, uint8_t tmax) {
  int diff = (int)b - (int)a;
  return (uint8_t)(a + (diff * (int)t) / (int)tmax);
}

// FIXED: New animation system - hold for 4s, fade for 1s
static void TwistManager_updatePaletteAnimation() {
  if (!TwistManager_usePaletteCycling || TwistManager_powerMode == TWIST_POWER_SAVING_LOCAL) return;
  if (!TwistManager_wledPowerState || TwistManager_paletteColorCount < 2) return;

  uint32_t now = millis();
  uint32_t elapsed = now - TwistManager_cycleStartTime;
  
  // Check if we need to advance to next color (every 5 seconds)
  if (elapsed >= TWIST_TOTAL_CYCLE_TIME_MS) {
    TwistManager_advanceToNextColor();
    TwistManager_cycleStartTime = now;
    elapsed = 0;
  }
  
  // Determine current phase
  if (elapsed < TWIST_COLOR_HOLD_TIME_MS) {
    // HOLD PHASE: Stay steady on current color
    TwistManager_currentDisplayColor = TwistManager_toColor;
  } else {
    // FADE PHASE: Cross-fade to next color
    uint32_t fadeElapsed = elapsed - TWIST_COLOR_HOLD_TIME_MS;
    uint8_t fadeProgress = (fadeElapsed * 255) / TWIST_COLOR_FADE_TIME_MS;
    fadeProgress = constrain(fadeProgress, 0, 255);
    
    // Get next color for fading
    uint8_t nextIndex = (TwistManager_currentPaletteIndex + 1) % TwistManager_paletteColorCount;
    TwistRGB nextColor = TwistManager_paletteColors[nextIndex];
    
    // Interpolate between current and next color
    TwistManager_currentDisplayColor.r = lerp8(TwistManager_toColor.r, nextColor.r, fadeProgress, 255);
    TwistManager_currentDisplayColor.g = lerp8(TwistManager_toColor.g, nextColor.g, fadeProgress, 255);
    TwistManager_currentDisplayColor.b = lerp8(TwistManager_toColor.b, nextColor.b, fadeProgress, 255);
  }
}

static void TwistManager_advanceToNextColor() {
  if (TwistManager_paletteColorCount < 2) return;
  
  // Move to next color
  TwistManager_currentPaletteIndex = (TwistManager_currentPaletteIndex + 1) % TwistManager_paletteColorCount;
  
  // Update from/to colors for next cycle
  TwistManager_fromColor = TwistManager_toColor;
  TwistManager_toColor = TwistManager_paletteColors[TwistManager_currentPaletteIndex];
  
  Serial.printf("[TWIST] Advanced to color %d: R=%d G=%d B=%d\n", 
                TwistManager_currentPaletteIndex,
                TwistManager_toColor.r, TwistManager_toColor.g, TwistManager_toColor.b);
}

static void TwistManager_handleRotation(int16_t diff) {
  int newBri = TwistManager_currentBrightness + (diff * TWIST_BRIGHTNESS_STEP);
  newBri = constrain(newBri, TWIST_MIN_BRIGHTNESS, TWIST_MAX_BRIGHTNESS);

  if (newBri != TwistManager_currentBrightness) {
    TwistManager_currentBrightness = newBri;
    if (WiFiManager_isConnected()) {
      if (!WLEDClient_sendBrightness((uint8_t)TwistManager_currentBrightness)) {
        Serial.printf("[TWIST] set bri=%d FAILED\n", TwistManager_currentBrightness);
      }
    }
    TwistManager_updateLED(); // local feedback
  }
}

// ENHANCED: Power toggle handling with comprehensive system control
static void TwistManager_handlePowerToggle() {
  static bool prevPressed = false;
  bool pressed = TwistManager_twist.isPressed();
  uint32_t now = millis();
  
  // Debounced button press detection
  if (pressed && !prevPressed && (now - TwistManager_lastPowerToggle > POWER_TOGGLE_DEBOUNCE_MS)) {
    TwistManager_lastPowerToggle = now;
    
    Serial.printf("[TWIST] Button pressed - current mode: %s\n", 
                  TwistManager_powerMode == TWIST_POWER_NORMAL_LOCAL ? "NORMAL" : "POWER_SAVING");
    
    if (TwistManager_powerMode == TWIST_POWER_NORMAL_LOCAL) {
      TwistManager_enterPowerSavingMode();
    } else {
      TwistManager_exitPowerSavingMode();
    }
  }
  
  prevPressed = pressed;
}

// ENHANCED: Enter power saving mode - FIXED to turn off screen immediately
static void TwistManager_enterPowerSavingMode() {
  Serial.println("[TWIST] Entering power saving mode");
  
  TwistManager_powerMode = TWIST_POWER_TRANSITION_LOCAL;
  
  // 1. Turn off WLED
  if (WiFiManager_isConnected()) {
    bool success = WLEDClient_setPowerState(false);
    Serial.printf("[TWIST] WLED power off: %s\n", success ? "OK" : "FAILED");
    TwistManager_wledPowerState = false;
  }
  
  // 2. Turn off encoder LED immediately
  TwistManager_twist.setColor(0, 0, 0);
  
  // 3. FIXED: Force screen off immediately by directly controlling backlight
  digitalWrite(PIN_LCD_BL, LOW);
  Serial.println("[TWIST] Power saving: WLED=OFF, Encoder=OFF, Screen=OFF immediately");
  
  TwistManager_powerMode = TWIST_POWER_SAVING_LOCAL;
}

// ENHANCED: Exit power saving mode  
static void TwistManager_exitPowerSavingMode() {
  Serial.println("[TWIST] Waking from power saving mode");
  
  TwistManager_powerMode = TWIST_POWER_TRANSITION_LOCAL;
  
  // 1. Turn on WLED
  if (WiFiManager_isConnected()) {
    bool success = WLEDClient_setPowerState(true);
    Serial.printf("[TWIST] WLED power on: %s\n", success ? "OK" : "FAILED");
    TwistManager_wledPowerState = true;
  }
  
  // 2. Wake screen by updating activity - FIXED
  DisplayManager_updateActivity();
  
  // 3. Force UI repaint to ensure screen is refreshed
  UIManager_forceRepaint();
  
  // 4. Re-sync palette and turn on encoder LED
  if (WiFiManager_isConnected()) {
    TwistManager_syncWithWLED();
    TwistManager_onProgramChange(); // Refresh palette colors
  }
  
  Serial.println("[TWIST] Wake up: WLED=ON, Screen=ON, Encoder synced");
  
  TwistManager_powerMode = TWIST_POWER_NORMAL_LOCAL;
}

static bool TwistManager_fetchWLEDPowerState() {
  JsonDocument doc;
  if (!WLEDClient_fetchWledState(doc)) { Serial.println("[TWIST] Fallback: power fetch failed"); return false; }
  bool newState = doc["state"]["on"].is<bool>() ? (bool)doc["state"]["on"] : TwistManager_wledPowerState;
  if (newState != TwistManager_wledPowerState) {
    TwistManager_wledPowerState = newState;
    Serial.printf("[TWIST] WLED is now %s\n", newState ? "ON" : "OFF");
    
    // If WLED state changed externally, update our power mode accordingly
    if (!newState && TwistManager_powerMode == TWIST_POWER_NORMAL_LOCAL) {
      Serial.println("[TWIST] WLED turned off externally - updating encoder display");
      TwistManager_updateLED(); // This will show the dim red indicator
    } else if (newState && TwistManager_powerMode == TWIST_POWER_SAVING_LOCAL) {
      Serial.println("[TWIST] WLED turned on externally while in power saving - staying in power saving");
      // Don't automatically exit power saving mode if WLED is turned on externally
    }
    
    return true;
  }
  return false;
}

// Use segment "Set Colors" if available (no TwistRGB types in signature)
static void TwistManager_buildFallbackFromSegment(const JsonObject& seg) {
  TwistManager_paletteColorCount = 0;
  if (!seg["col"].is<JsonArray>()) return;

  for (JsonVariant cv : seg["col"].as<JsonArray>()) {
    if (!cv.is<JsonArray>()) continue;
    JsonArray ca = cv.as<JsonArray>();
    if (ca.size() < 3) continue;

    uint8_t rin = (uint8_t)ca[0];
    uint8_t gin = (uint8_t)ca[1];
    uint8_t bin = (uint8_t)ca[2];
    uint8_t r, g, b; fromWledOrder(rin, gin, bin, r, g, b);
    if ((r | g | b) == 0) continue; // skip black

    // dedup
    bool dup = false;
    for (uint8_t i=0;i<TwistManager_paletteColorCount;i++) {
      const TwistRGB& p = TwistManager_paletteColors[i];
      if (isNearRGB(p.r, p.g, p.b, r, g, b, 8)) { dup = true; break; }
    }
    if (!dup && TwistManager_paletteColorCount < 16) {
      TwistManager_paletteColors[TwistManager_paletteColorCount++] = MAKE_RGB(r,g,b);
    }
  }
}

// ROYGBIV fallback (no params → no TwistRGB in prototype)
static void TwistManager_useFallbackROYGBIV() {
  static const uint8_t roygbiv[7][3] = {
    {255,   0,   0},  // Red
    {255, 127,   0},  // Orange
    {255, 255,   0},  // Yellow
    {  0, 255,   0},  // Green
    {  0,   0, 255},  // Blue
    { 75,   0, 130},  // Indigo
    {148,   0, 211}   // Violet
  };

  TwistManager_paletteColorCount = 7;
  for (uint8_t i=0;i<7;i++) {
    TwistManager_paletteColors[i] = MAKE_RGB(roygbiv[i][0], roygbiv[i][1], roygbiv[i][2]);
  }

  TwistManager_usePaletteCycling = true;
  TwistManager_currentPaletteIndex = 0;
  TwistManager_fromColor = TwistManager_paletteColors[0];
  TwistManager_toColor = TwistManager_paletteColors[0];
  TwistManager_currentDisplayColor = TwistManager_toColor;
  TwistManager_cycleStartTime = millis();

  Serial.printf("[TWIST] Palette fallback: ROYGBIV (%u colors)\n", TwistManager_paletteColorCount);
}

#if TWIST_ENABLE_JSON_LIVE
// Try sampling actual LED colors via /json/live (optional WLED feature)
static void TwistManager_tryJsonLiveSample() {
  if (!WiFiManager_isConnected()) return;

  uint32_t now = millis();
  if (now - TwistManager_lastLiveSample < TWIST_JSON_LIVE_MIN_INTERVAL_MS) return;
  TwistManager_lastLiveSample = now;

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(400);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  String url = String("http://") + WLED_IP + "/json/live";
  if (!http.begin(client, url)) { http.end(); Serial.println("[TWIST] live: begin() failed"); return; }

  int code = http.GET();
  if (code != 200) { http.end(); Serial.printf("[TWIST] live: GET failed (%d)\n", code); return; }

  String payload = http.getString();
  http.end();

  JsonDocument live;
  if (deserializeJson(live, payload)) { Serial.println("[TWIST] live: JSON parse error"); return; }
  if (!live["leds"].is<JsonArray>()) { Serial.println("[TWIST] live: leds[] missing"); return; }

  JsonArray leds = live["leds"];
  if (leds.size() == 0) { Serial.println("[TWIST] live: leds[] empty"); return; }

  // Reset and sample up to 8 evenly spaced LEDs
  TwistManager_paletteColorCount = 0;
  const int samples = min(8, (int)leds.size());
  for (int k=0; k<samples && TwistManager_paletteColorCount < 16; k++) {
    int idx = (k * ((int)leds.size() - 1)) / max(1, samples - 1);
    JsonVariant v = leds[idx];

    uint8_t r=0,g=0,b=0;
    if (v.is<const char*>()) {
      const char* s = v.as<const char*>(); // "RRGGBB"
      if (s && strlen(s) >= 6) {
        unsigned long rgb = strtoul(s, nullptr, 16);
        uint8_t rin = (rgb >> 16) & 0xFF;
        uint8_t gin = (rgb >> 8)  & 0xFF;
        uint8_t bin =  rgb        & 0xFF;
        fromWledOrder(rin, gin, bin, r, g, b);
      }
    } else if (v.is<JsonArray>()) {
      JsonArray arr = v.as<JsonArray>();
      if (arr.size() >= 3) {
        uint8_t rin = (uint8_t)arr[0];
        uint8_t gin = (uint8_t)arr[1];
        uint8_t bin = (uint8_t)arr[2];
        fromWledOrder(rin, gin, bin, r, g, b);
      }
    }

    if ((r | g | b) == 0) continue;

    bool dup = false;
    for (uint8_t i=0;i<TwistManager_paletteColorCount;i++) {
      const TwistRGB& p = TwistManager_paletteColors[i];
      if (isNearRGB(p.r, p.g, p.b, r, g, b)) { dup = true; break; }
    }
    if (!dup) {
      TwistManager_paletteColors[TwistManager_paletteColorCount++] = MAKE_RGB(r,g,b);
    }
  }

  Serial.printf("[TWIST] live: sampled %u colors\n", TwistManager_paletteColorCount);
}
#endif // TWIST_ENABLE_JSON_LIVE

// Build palette from WLED state; fallback to ROYGBIV when needed
void TwistManager_fetchWLEDPalette() {
  if (!WiFiManager_isConnected()) { 
    Serial.println("[TWIST] Fallback: WiFi down"); 
    TwistManager_useFallbackROYGBIV(); 
    return; 
  }

  JsonDocument doc;
  if (!WLEDClient_fetchWledState(doc)) { 
    Serial.println("[TWIST] Fallback: state fetch failed"); 
    TwistManager_useFallbackROYGBIV(); 
    return; 
  }

  if (!doc["state"]["seg"].is<JsonArray>()) { 
    Serial.println("[TWIST] Fallback: no seg[] in state"); 
    TwistManager_useFallbackROYGBIV(); 
    return; 
  }
  
  JsonArray segs = doc["state"]["seg"];
  if (segs.size() == 0) { 
    Serial.println("[TWIST] Fallback: seg[] empty"); 
    TwistManager_useFallbackROYGBIV(); 
    return; 
  }
  JsonObject seg0 = segs[0];

  int paletteId = seg0["pal"].is<int>() ? (int)seg0["pal"] : -1;

  // 1) Try "Set Colors" fast path
  TwistManager_paletteColorCount = 0;
  if (paletteId == 4 /*Set Colors*/ || paletteId == 5 /*Based on Set Colors*/) {
    TwistManager_buildFallbackFromSegment(seg0);
    if (TwistManager_paletteColorCount >= 2) {
      Serial.printf("[TWIST] seg.col yielded %u colors\n", TwistManager_paletteColorCount);
    } else {
      Serial.println("[TWIST] seg.col yielded <2 colors");
    }
  } else {
    Serial.printf("[TWIST] pal=%d (not Set Colors); trying /json/live\n", paletteId);
  }

  // 2) If not enough colors, take a quick sample of actual output
  #if TWIST_ENABLE_JSON_LIVE
  if (TwistManager_paletteColorCount < 2) {
    TwistManager_tryJsonLiveSample();
  }
  #endif

  // 3) Still not enough? ROYGBIV fallback
  if (TwistManager_paletteColorCount < 2) {
    Serial.println("[TWIST] Fallback: <2 colors after seg/liv; using ROYGBIV");
    TwistManager_useFallbackROYGBIV();
    return;
  }

  // Arm palette cycler with new hold/fade timing
  TwistManager_usePaletteCycling = true;
  TwistManager_currentPaletteIndex = 0;
  TwistManager_fromColor = TwistManager_paletteColors[0];
  TwistManager_toColor = TwistManager_paletteColors[0];
  TwistManager_currentDisplayColor = TwistManager_toColor;
  TwistManager_cycleStartTime = millis();

  Serial.printf("[TWIST] Palette ready: %u colors (hold/fade cycling)\n", TwistManager_paletteColorCount);
}

// Missing power management functions
void TwistManager_enterPowerSave() {
  // Turn off RGB LED to save power
  TwistManager_twist.setColor(0, 0, 0);
  Serial.println("[TWIST] Entered power save mode");
}

void TwistManager_exitPowerSave() {
  // Restore RGB LED to current color
  TwistManager_twist.setColor(TwistManager_currentDisplayColor.r,
                             TwistManager_currentDisplayColor.g,
                             TwistManager_currentDisplayColor.b);
  Serial.println("[TWIST] Exited power save mode");
}