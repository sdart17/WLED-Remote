// ─────────────────────────────────────────────────────────────
// User Interface Management - OPTIMIZED with Brightness Page Control
// FIXED: Prevent touch events during swipe navigation
// ─────────────────────────────────────────────────────────────

// Font experimentation - use built-in fonts with different sizes for better appearance
// Note: External fonts disabled temporarily to isolate crash issues

// Text-based icons using simple ASCII symbols - safe alternative to bitmap icons
const char* ICON_POWER = "O";        // Power symbol (O for on/off)
const char* ICON_UP = "^";           // Up arrow  
const char* ICON_DOWN = "v";         // Down arrow
const char* ICON_LEFT = "<";         // Left arrow
const char* ICON_RIGHT = ">";        // Right arrow
const char* ICON_SHUFFLE = "?";      // Shuffle/random
const char* ICON_BRIGHT = "*";       // Brightness/sun
const char* ICON_MUSIC = "~";        // Music note
const char* ICON_ONE = "1";          // Number 1
const char* ICON_TWO = "2";          // Number 2
const char* ICON_THREE = "3";        // Number 3

// UI manager state variables
static int UIManager_currentPage = 0;
static BrightnessSlider UIManager_brightnessSlider = { 50, 60, 140, 220, "/370x140-brightness-bw.bmp", 128 };
static int16_t UIManager_touchStartX = -1;
static int16_t UIManager_touchStartY = -1;
static bool UIManager_touchMoved = false;
static uint32_t UIManager_touchStartTime = 0;

// Now Playing auto-refresh and state tracking
static uint32_t UIManager_lastNowPlayingUpdate = 0;
static const uint32_t NOW_PLAYING_UPDATE_INTERVAL_MS = 10000; // 10 seconds

// Track previous state for selective updates
static int UIManager_lastPresetId = -1;
static int UIManager_lastPlaylistId = -1;
static int UIManager_lastEffectId = -1;
static int UIManager_lastPaletteId = -1;
static bool UIManager_forceNowPlayingUpdate = true; // Force first update

// FIXED: Add swipe detection and suppression
static bool UIManager_swipeDetected = false;
static const uint32_t SWIPE_SUPPRESS_TIME_MS = 300; // Suppress touches for 300ms after swipe
static uint32_t UIManager_lastSwipeTime = 0;

// OPTIMIZED: Smart UI state tracking
static bool UIManager_needsFullRepaint = true;
static int UIManager_lastBrightnessValue = -1;
static bool UIManager_lastScreenState = true;
static bool UIManager_earlyBootMode = true;

// OPTIMIZED: Button press tracking for visual feedback
static int UIManager_pressedButtonIndex = -1;
static uint32_t UIManager_buttonPressTime = 0;
static const uint32_t BUTTON_PRESS_FEEDBACK_MS = 150;

// ENHANCED: Quick Launch Actions - Expanded to 6 slots  
#define ACTION_QUICK_LAUNCH_1    201
#define ACTION_QUICK_LAUNCH_2    202
#define ACTION_QUICK_LAUNCH_3    203
#define ACTION_QUICK_LAUNCH_4    204  // Shuffle (QL4)
#define ACTION_QUICK_LAUNCH_5    205  // Bright White (QL5)
#define ACTION_QUICK_LAUNCH_6    206  // Music (QL6)

// Dropdown state for instance selection
typedef struct {
  int16_t x, y, w, h;
} DropdownBounds;
static DropdownBounds UIManager_dropdownBounds = {0, 0, 0, 0};
static bool UIManager_dropdownExpanded = false;

// Page 0 buttons - ENHANCED: All 6 buttons now use quick launch mappings  
static IconBtn UIManager_page0Buttons[] = {
  { 30,  40, 80, 80, "/shuffle-80x80-wb-border.bmp", "/shuffle-80x80-bw-border.bmp", "SHFFL", ACTION_QUICK_LAUNCH_4},
  { 30, 130, 80, 80, "/bright-80x80-wb-border.bmp",  "/bright-80x80-bw-border.bmp",  "BRGHT", ACTION_QUICK_LAUNCH_5},
  { 30, 220, 80, 80, "/music-80x80-wb-border.bmp",   "/music-80x80-bw-border.bmp",   "MUSIC", ACTION_QUICK_LAUNCH_6},
  {130,  40, 80, 80, "/one-80x80-wb-border.bmp",     "/one-80x80-bw-border.bmp",     "ONE", ACTION_QUICK_LAUNCH_1},
  {130, 130, 80, 80, "/two-80x80-wb-border.bmp",     "/two-80x80-bw-border.bmp",     "TWO", ACTION_QUICK_LAUNCH_2},
  {130, 220, 80, 80, "/three-80x80-wb-border.bmp",   "/three-80x80-bw-border.bmp",   "THREE", ACTION_QUICK_LAUNCH_3}
};
static const int UIManager_PAGE0_BTN_COUNT = 6;

// Page 1 buttons
static IconBtn UIManager_page1Buttons[] = {
  { 80,  20, 80, 80, "/power-80x80-wb-border.bmp", "/power-80x80-bw-border.bmp", "PWR", 10},
  { 80, 120, 80, 80, "/up-80x80-wb-border.bmp",    "/up-80x80-bw-border.bmp",    "UP",  11},
  { 80, 200, 80, 80, "/down-80x80-wb-border.bmp",  "/down-80x80-bw-border.bmp",  "DOWN",12},
  {160, 160, 80, 80, "/right-80x80-wb-border.bmp", "/right-80x80-bw-border.bmp", "RT",  13},
  {  0, 160, 80, 80, "/left-80x80-wb-border.bmp",  "/left-80x80-bw-border.bmp",  "LT",  14}
};
static const int UIManager_PAGE1_BTN_COUNT = 5;

// Page 3 buttons (System) - Note: Page 2 is brightness slider if enabled
static IconBtn UIManager_page3Buttons[] = {
  { 20,  90, 200, 60, nullptr, nullptr, "Shutdown", 201 },
  { 20, 180, 200, 60, nullptr, nullptr, "Restart",  202 }
};
static const int UIManager_PAGE3_BTN_COUNT = 2;

// ENHANCED: Page mapping function to handle optional pages (now playing + brightness + WLED selection)
static int UIManager_mapToPhysicalPage(int logicalPage) {
  // Safety check: ensure logical page is within valid range
  if (logicalPage < 0) return 0;
  if (logicalPage >= TOTAL_UI_PAGES) return 0;
  
  // New page layout:
  // Physical 0: Main controls (logical 0) - always present
  // Physical 1: Navigation controls (logical 1) - always present  
  // Physical 2: Now Playing with Instance Dropdown (logical 2) - if ENABLE_NOW_PLAYING_PAGE
  // Physical 3: Brightness slider (logical 3) - if ENABLE_BRIGHTNESS_PAGE
  // Physical 4: System controls - always present (logical varies based on optional pages)
  // Note: WLED selection merged into Now Playing page
  
  if (logicalPage <= 1) {
    return logicalPage; // Pages 0,1 always direct mapping
  }
  
  int physicalPage = 2; // Start from physical page 2
  
  // Page 2: Now Playing (if enabled)
  if (ENABLE_NOW_PLAYING_PAGE) {
    if (logicalPage == 2) return 2;
    physicalPage = 3;
  }
  
  // Page 3: Brightness (if enabled) 
  if (ENABLE_BRIGHTNESS_PAGE) {
    if (logicalPage == (ENABLE_NOW_PLAYING_PAGE ? 3 : 2)) return physicalPage;
    physicalPage++;
  }
  
  // System page
  int systemLogicalPage = 2;
  if (ENABLE_NOW_PLAYING_PAGE) systemLogicalPage++;
  if (ENABLE_BRIGHTNESS_PAGE) systemLogicalPage++;
  
  if (logicalPage == systemLogicalPage) return physicalPage;
  
  // WLED Selection page is now merged into Now Playing - no separate page needed
  
  return physicalPage;
}

static int UIManager_mapFromPhysicalPage(int physicalPage) {
  // Safety check
  if (physicalPage < 0) return 0;
  
  if (physicalPage <= 1) {
    return physicalPage; // Pages 0,1 always direct mapping
  }
  
  // Reverse mapping based on enabled pages
  int logicalPage = 2; // Start from logical page 2
  
  // Physical page 2: Now Playing
  if (physicalPage == 2 && ENABLE_NOW_PLAYING_PAGE) {
    return 2;
  }
  if (!ENABLE_NOW_PLAYING_PAGE && physicalPage == 2) {
    // If now playing disabled, physical page 2 could be brightness or system
    if (ENABLE_BRIGHTNESS_PAGE) return 2;
    return 2; // System page
  }
  
  // Physical page 3: Brightness (if now playing enabled) or system
  if (physicalPage == 3) {
    if (ENABLE_NOW_PLAYING_PAGE && ENABLE_BRIGHTNESS_PAGE) return 3;
    if (ENABLE_NOW_PLAYING_PAGE && !ENABLE_BRIGHTNESS_PAGE) return 3; // System
    if (!ENABLE_NOW_PLAYING_PAGE && ENABLE_BRIGHTNESS_PAGE) return 2;
    return 2; // System
  }
  
  // Physical page 4+: System or WLED selection
  if (ENABLE_NOW_PLAYING_PAGE) logicalPage++;
  if (ENABLE_BRIGHTNESS_PAGE) logicalPage++;
  
  // Clamp to valid range
  if (logicalPage >= TOTAL_UI_PAGES) logicalPage = TOTAL_UI_PAGES - 1;
  
  return logicalPage;
}

bool UIManager_hitTest(const IconBtn& b, int16_t x, int16_t y) {
  return (x >= b.x && x < (b.x + b.w) && y >= b.y && y < (b.y + b.h));
}

// OPTIMIZED: Enhanced text drawing with better performance
void UIManager_drawLabelCentered(int16_t x, int16_t y, int16_t w, int16_t h,
                                const char* s, uint16_t fg, uint16_t bg, uint8_t sz = 2) {
  // CRASH FIX: Verify display is ready before using TFT
  if (!DisplayManager_isScreenOn()) {
    Serial.println("[UI] Cannot draw label - screen is off");
    return;
  }
  
  auto& tft = DisplayManager_getTFT();
  tft.setTextWrap(false);
  tft.setTextSize(sz);
  tft.setTextColor(fg, bg);
  
  int16_t bx, by; 
  uint16_t bw, bh;
  tft.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  
  int16_t cx = x + (w - (int)bw) / 2;
  int16_t cy = y + (h - (int)bh) / 2;
  
  // OPTIMIZED: Use DisplayManager's optimized functions
  DisplayManager_drawButtonRect(x, y, w, h, fg, bg);
  tft.setCursor(cx, cy);
  tft.print(s);
}

// OPTIMIZED: Smart icon button drawing with minimal redraws
void UIManager_drawIconButton(const IconBtn& btn, bool pressed) {
  uint16_t fg = pressed ? ST77XX_BLACK : ST77XX_WHITE;
  uint16_t bg = pressed ? ST77XX_WHITE : ST77XX_BLACK;

  // CRITICAL FIX: During early boot (first 15 seconds), only use fallback text
  // This prevents hanging on SD card access when icons are preloaded but SD is unmounted

  bool iconDrawn = false;
  
  if (!UIManager_earlyBootMode) {
    const char* iconToLoad = (pressed && btn.iconPressedPath) ? btn.iconPressedPath : btn.iconPath;
    if (iconToLoad && SDManager_isAvailable()) {
      iconDrawn = SDManager_drawBMPFromSD(iconToLoad, btn.x, btn.y);
      if (iconDrawn) {
        Serial.printf("[UI] Drew icon: %s\n", iconToLoad);
        return;
      } else {
        Serial.printf("[UI] Failed to draw icon: %s\n", iconToLoad);
      }
    }
  }
  
  // Use fallback text (always during early boot, or if icon loading fails)
  const char* label = btn.fallbackText ? btn.fallbackText : "";
  
  // CRITICAL FIX: Use direct TFT text drawing to avoid DisplayManager issues
  auto& tft = DisplayManager_getTFT();
  
  // CRASH FIX: Verify TFT object is valid before using it
  // Check if display is properly initialized by testing if screen is on
  if (!DisplayManager_isScreenOn()) {
    Serial.println("[UI] Cannot draw - screen is off or TFT not initialized");
    return;
  }
  
  tft.fillRect(btn.x, btn.y, btn.w, btn.h, bg);
  tft.drawRect(btn.x, btn.y, btn.w, btn.h, fg);
  
  // Simple centered text
  tft.setTextSize(2);
  tft.setTextColor(fg, bg);
  
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  
  int16_t textX = btn.x + (btn.w - w) / 2;
  int16_t textY = btn.y + (btn.h - h) / 2;
  
  tft.setCursor(textX, textY);
  tft.print(label);
  
  Serial.printf("[UI] Drew direct text: %s at (%d,%d)\n", label, textX, textY);
}

// EXPERIMENTAL: Modern text-based button drawing with smooth fonts
void UIManager_drawTextButton(const IconBtn& btn, bool pressed, const char* iconText) {
  // CRASH FIX: Verify display is ready before using TFT
  if (!DisplayManager_isScreenOn()) {
    Serial.println("[UI] Cannot draw text button - screen is off");
    return;
  }
  
  uint16_t fg = pressed ? ST77XX_BLACK : ST77XX_WHITE;
  uint16_t bg = pressed ? ST77XX_WHITE : ST77XX_BLACK;

  auto& tft = DisplayManager_getTFT();
  
  // Draw button background with rounded appearance
  tft.fillRect(btn.x, btn.y, btn.w, btn.h, bg);
  tft.drawRect(btn.x, btn.y, btn.w, btn.h, fg);
  
  // Use built-in font for icon text
  if (iconText && strlen(iconText) > 0) {
    tft.setTextSize(2); // Use larger built-in font instead of external font
    tft.setTextColor(fg, bg);
    
    // Center the text icon
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(iconText, 0, 0, &x1, &y1, &w, &h);
    
    int16_t textX = btn.x + (btn.w - w) / 2;
    int16_t textY = btn.y + (btn.h + h) / 2 - 3; // Adjust for font baseline
    
    tft.setCursor(textX, textY);
    tft.print(iconText);
    tft.setTextSize(1); // Reset to default text size
    
    Serial.printf("[UI] Drew text icon: %s at (%d,%d)\n", iconText, textX, textY);
  } else {
    // Fallback to regular text
    UIManager_drawIconButton(btn, pressed);
  }
}

#if ENABLE_BRIGHTNESS_PAGE
// FIXED: Brightness slider with proper drawing logic (only compiled if enabled)
void UIManager_drawBrightnessSlider() {
  int16_t sliderX = 50, sliderY = 60, sliderW = 140, sliderH = 220;
  auto& tft = DisplayManager_getTFT();
  
  // Always draw the complete slider for page 2 
  // Clear the entire slider area first
  tft.fillRect(sliderX - 5, sliderY - 5, sliderW + 10, sliderH + 10, ST77XX_BLACK);
  
  // Draw slider border
  tft.drawRect(sliderX, sliderY, sliderW, sliderH, ST77XX_WHITE);
  
  // Calculate fill height based on brightness value
  int16_t fillH = (sliderH - 4) * UIManager_brightnessSlider.value / 255;
  
  // Draw brightness fill from bottom up
  if (fillH > 0) {
    int16_t fillY = sliderY + sliderH - 2 - fillH;
    tft.fillRect(sliderX + 2, fillY, sliderW - 4, fillH, ST77XX_WHITE);
  }
  
  // Draw brightness percentage text
  char percentText[8];
  int percentage = (UIManager_brightnessSlider.value * 100) / 255;
  snprintf(percentText, sizeof(percentText), "%d%%", percentage);
  
  // Position text below slider
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  int16_t bx, by; 
  uint16_t bw, bh;
  tft.getTextBounds(percentText, 0, 0, &bx, &by, &bw, &bh);
  int16_t textX = sliderX + (sliderW - bw) / 2;
  int16_t textY = sliderY + sliderH + 10;
  
  tft.setCursor(textX, textY);
  tft.print(percentText);
  
  // Update slider position for touch detection
  UIManager_brightnessSlider.x = sliderX; 
  UIManager_brightnessSlider.y = sliderY;
  UIManager_brightnessSlider.w = sliderW; 
  UIManager_brightnessSlider.h = sliderH;
  
  UIManager_lastBrightnessValue = UIManager_brightnessSlider.value;
}

bool UIManager_inSliderActiveLane(int16_t x, int16_t y) {
  int16_t cx = UIManager_brightnessSlider.x + UIManager_brightnessSlider.w / 2;
  int16_t ax1 = cx - SLIDER_ACTIVE_HALF_WIDTH;
  int16_t ax2 = cx + SLIDER_ACTIVE_HALF_WIDTH;
  int16_t ay1 = UIManager_brightnessSlider.y - SLIDER_Y_PAD;
  int16_t ay2 = UIManager_brightnessSlider.y + UIManager_brightnessSlider.h + SLIDER_Y_PAD;
  return (x >= ax1 && x < ax2 && y >= ay1 && y < ay2);
}
#endif // ENABLE_BRIGHTNESS_PAGE

#if ENABLE_NOW_PLAYING_PAGE
// Basic effect name mapping for common WLED effects
const char* UIManager_getEffectName(int effectId) {
  switch (effectId) {
    case 0: return "Solid";
    case 1: return "Blink";
    case 2: return "Breathe";
    case 3: return "Wipe";
    case 4: return "Wipe Random";
    case 5: return "Random Colors";
    case 6: return "Sweep";
    case 7: return "Dynamic";
    case 8: return "Colorloop";
    case 9: return "Rainbow";
    case 10: return "Scan";
    case 11: return "Dual Scan";
    case 12: return "Fade";
    case 13: return "Theater Chase";
    case 14: return "Theater Rainbow";
    case 15: return "Running Lights";
    case 16: return "Saw";
    case 17: return "Twinkle";
    case 18: return "Dissolve";
    case 19: return "Dissolve Rnd";
    case 20: return "Sparkle";
    case 21: return "Flash Sparkle";
    case 22: return "Hyper Sparkle";
    case 23: return "Strobe";
    case 24: return "Strobe Rainbow";
    case 25: return "Multi Strobe";
    case 26: return "Blink Rainbow";
    case 27: return "Chase White";
    case 28: return "Chase Color";
    case 29: return "Chase Random";
    case 30: return "Chase Rainbow";
    case 31: return "Chase Flash";
    case 32: return "Chase Flash Rnd";
    case 33: return "Rainbow Runner";
    case 34: return "Colorful";
    case 35: return "Traffic Light";
    case 36: return "Sweep Random";
    case 37: return "Running 2";
    case 38: return "Red & Blue";
    case 39: return "Stream";
    case 40: return "Scanner";
    case 41: return "Lighthouse";
    case 42: return "Fireworks";
    case 43: return "Rain";
    case 44: return "Merry Christmas";
    case 45: return "Fire Flicker";
    case 46: return "Gradient";
    case 47: return "Loading";
    case 48: return "Police";
    case 49: return "Police All";
    case 50: return "Two Dots";
    case 51: return "Fairytwinkle";
    case 52: return "Circus";
    case 53: return "Halloween";
    case 54: return "Tri Chase";
    case 55: return "Tri Wipe";
    case 56: return "Tri Fade";
    case 57: return "Lightning";
    case 58: return "ICU";
    case 59: return "Multi Comet";
    case 60: return "Scanner Dual";
    case 61: return "Stream 2";
    case 62: return "Oscillate";
    case 63: return "Pride 2015";
    case 64: return "Juggle";
    case 65: return "Palette";
    case 66: return "Fire 2012";
    case 67: return "Colorwaves";
    case 68: return "BPM";
    case 69: return "Fill Noise";
    case 70: return "Noise 1";
    case 71: return "Noise 2";
    case 72: return "Noise 3";
    case 73: return "Noise 4";
    case 74: return "Colortwinkles";
    case 75: return "Lake";
    case 76: return "Meteor";
    case 77: return "Meteor Smooth";
    case 78: return "Railway";
    case 79: return "Ripple";
    default: return nullptr;
  }
}

// Basic palette name mapping for common WLED palettes  
const char* UIManager_getPaletteName(int paletteId) {
  switch (paletteId) {
    case 0: return "Default";
    case 1: return "Random Cycle";
    case 2: return "Primary Color";
    case 3: return "Based on Set";
    case 4: return "Party";
    case 5: return "Cloud";
    case 6: return "Lava";
    case 7: return "Ocean";
    case 8: return "Forest";
    case 9: return "Rainbow";
    case 10: return "Rainbow Bands";
    case 11: return "Sunset";
    case 12: return "Rivendell";
    case 13: return "Breeze";
    case 14: return "Red & Blue";
    case 15: return "Yellowout";
    case 16: return "Analogous";
    case 17: return "Splash";
    case 18: return "Pastel";
    case 19: return "Sunset 2";
    case 20: return "Beech";
    case 21: return "Vintage";
    case 22: return "Departure";
    case 23: return "Landscape";
    case 24: return "Beach";
    case 25: return "Sherbet";
    case 26: return "Hult";
    case 27: return "Hult 64";
    case 28: return "Hult";
    case 29: return "Jul";
    case 30: return "Grintage";
    case 31: return "Rewhi";
    case 32: return "Tertiary";
    case 33: return "Fire";
    case 34: return "Icefire";
    case 35: return "Cyane";
    case 36: return "Light Pink";
    case 37: return "Autumn";
    case 38: return "Magenta";
    case 39: return "Magred";
    case 40: return "Yelmag";
    case 41: return "Yelblu";
    case 42: return "Orange & Teal";
    case 43: return "Tiamat";
    case 44: return "April Night";
    case 45: return "Orangery";
    case 46: return "C9";
    case 47: return "Sakura";
    case 48: return "Aurora";
    case 49: return "Atlantica";
    case 50: return "C9 2";
    case 58: return "Toxy Reef";
    default: return nullptr;
  }
}

// Enhanced Now Playing Page with Instance Dropdown - Display comprehensive WLED state information
void UIManager_drawNowPlayingPage() {
  // CRASH FIX: Verify display is ready before using TFT
  if (!DisplayManager_isScreenOn()) {
    Serial.println("[UI] Cannot draw now playing - screen is off");
    return;
  }
  
  auto& tft = DisplayManager_getTFT();
  
  // Clear the page area first
  tft.fillRect(0, 20, 240, 300, ST77XX_BLACK);
  
  // Draw instance dropdown at top instead of "Now Playing" header
  UIManager_drawInstanceDropdown();
  
  // Prepare to fetch WLED state - use explicit size for reliability
  JsonDocument doc;
  
  // Debug: Show current WLED IP being used and memory status
  const char* currentIP = getWLEDIP();
  Serial.printf("[UI] Now Playing: Fetching state from WLED IP: %s (instance %d)\n", 
                currentIP, CURRENT_WLED_INSTANCE);
  Serial.printf("[UI] WiFi connected: %s\n", WiFiManager_isConnected() ? "YES" : "NO");
  Serial.printf("[UI] Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Add a small loading indicator on screen during fetch
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(10, 60);
  tft.print("Loading...");
  
  bool dataAvailable = WLEDClient_fetchWledState(doc);
  Serial.printf("[UI] WLED state fetch result: %s (heap after: %d bytes)\n", 
                dataAvailable ? "SUCCESS" : "FAILED", ESP.getFreeHeap());
  
  if (dataAvailable) {
    UIManager_drawEnhancedWLEDStateInfo(doc);
  } else {
    // Show error message with detailed debugging info
    tft.fillRect(10, 60, 220, 180, ST77XX_BLACK); // Clear the area first
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.setCursor(10, 60);
    tft.print("Cannot connect to WLED");
    tft.setCursor(10, 75);
    tft.printf("IP: %s", currentIP);
    tft.setCursor(10, 90);
    tft.printf("WiFi: %s", WiFiManager_isConnected() ? "Connected" : "Disconnected");
    tft.setCursor(10, 105);
    tft.printf("Instance: %d/%d", CURRENT_WLED_INSTANCE + 1, WLED_INSTANCE_COUNT);
    tft.setCursor(10, 120);
    tft.printf("Heap: %d bytes", ESP.getFreeHeap());
    
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setCursor(10, 140);
    tft.print("Try refresh button below");
    
    // BUGFIX: Show refresh button even when connection fails
    int yPos = 280;
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.drawRect(80, yPos, 80, 30, ST77XX_WHITE);
    tft.setCursor(95, yPos + 8);
    tft.print("Refresh");
  }
}

// Get actual preset name from WLED presets.json (dynamic version)
bool UIManager_getActualPresetName(int presetId, char* buffer, size_t bufferSize) {
  if (presetId <= 0 || !buffer || bufferSize == 0) {
    return false;
  }
  
  // Try to fetch actual name from WLED
  if (WLEDClient_fetchPresetName(presetId, buffer, bufferSize)) {
    return true;
  }
  
  // Fallback to static mappings for known presets
  switch (presetId) {
    case 1: strncpy(buffer, "Default", bufferSize - 1); break;
    case 2: strncpy(buffer, "Colorful", bufferSize - 1); break;
    case 3: strncpy(buffer, "Aurora", bufferSize - 1); break;
    case 4: strncpy(buffer, "Party", bufferSize - 1); break;
    case 5: strncpy(buffer, "Relaxing", bufferSize - 1); break;
    case 6: strncpy(buffer, "Pride", bufferSize - 1); break;
    case 7: strncpy(buffer, "Rainbow", bufferSize - 1); break;
    case 8: strncpy(buffer, "Fire", bufferSize - 1); break;
    case 9: strncpy(buffer, "Ocean", bufferSize - 1); break;
    case 10: strncpy(buffer, "Forest", bufferSize - 1); break;
    case 20: strncpy(buffer, "Fairytwinkle", bufferSize - 1); break;
    case 28: strncpy(buffer, "Noise 4", bufferSize - 1); break;
    case 30: strncpy(buffer, "Default", bufferSize - 1); break;
    default: return false; // Unknown preset
  }
  
  buffer[bufferSize - 1] = '\0';
  return true;
}

// Keep original function for compatibility
const char* UIManager_getPresetName(int presetId) {
  switch (presetId) {
    case 1: return "Default";
    case 2: return "Colorful";  
    case 3: return "Aurora";
    case 4: return "Party";
    case 5: return "Relaxing";
    case 6: return "Pride";
    case 7: return "Rainbow";
    case 8: return "Fire";
    case 9: return "Ocean";
    case 10: return "Forest";
    case 20: return "Fairytwinkle";
    case 28: return "Noise 4";
    case 30: return "Default";
    default: return nullptr;
  }
}

// Fetch actual effect name from WLED /json/effects endpoint
bool UIManager_getWLEDEffectName(int effectId, char* buffer, size_t bufferSize) {
  // Try to fetch actual name from WLED first
  if (WLEDClient_fetchEffectName(effectId, buffer, bufferSize)) {
    Serial.printf("[DEBUG] Using WLED effect name: %s\n", buffer);
    return true;
  }
  
  // Fallback to static lookup
  const char* name = UIManager_getEffectName(effectId);
  
  Serial.printf("[DEBUG] Effect lookup fallback: ID=%d, name=%s\n", effectId, name ? name : "NULL");
  
  if (name && strlen(name) > 0) {
    strncpy(buffer, name, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    Serial.printf("[DEBUG] Using fallback effect name: %s\n", buffer);
    return true;
  }
  
  // Final fallback to ID-based name
  snprintf(buffer, bufferSize, "Effect %d", effectId);
  Serial.printf("[DEBUG] Using ID-based effect name: %s\n", buffer);
  return false; // Indicate we used fallback
}

// Fetch actual palette name from WLED /json/palettes endpoint  
bool UIManager_getWLEDPaletteName(int paletteId, char* buffer, size_t bufferSize) {
  // Try to fetch actual name from WLED first
  if (WLEDClient_fetchPaletteName(paletteId, buffer, bufferSize)) {
    Serial.printf("[DEBUG] Using WLED palette name: %s\n", buffer);
    return true;
  }
  
  // Fallback to static lookup
  const char* name = UIManager_getPaletteName(paletteId);
  
  Serial.printf("[DEBUG] Palette lookup fallback: ID=%d, name=%s\n", paletteId, name ? name : "NULL");
  
  if (name && strlen(name) > 0) {
    strncpy(buffer, name, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    Serial.printf("[DEBUG] Using fallback palette name: %s\n", buffer);
    return true;
  }
  
  // Final fallback to ID-based name
  snprintf(buffer, bufferSize, "Palette %d", paletteId);
  Serial.printf("[DEBUG] Using ID-based palette name: %s\n", buffer);
  return false; // Indicate we used fallback
}

// Check if an effect ignores palettes (generates its own colors)
bool UIManager_effectIgnoresPalette(int effectId, const char* effectName) {
  // Effects that ignore palettes and generate their own colors
  const char* noPaletteEffects[] = {
    "Solid",
    "Blink", 
    "Breathe",
    "Wipe",
    "Wipe Random",
    "Random Colors",
    "Sweep",
    "Dynamic",
    "Colorloop",
    "Rainbow",
    "Rainbow Runner",
    "Distortion Waves",
    "Plasma",
    "Flow",
    "Chunchun",
    "Dancing Shadows",
    "Washing Machine",
    "TV Simulator",
    "Aurora",
    "Waverly",
    "Sun Radiation",
    "Colored Bursts",
    "Julia",
    "Ripple Peak",
    "2D Cellular Automata",
    "2D Colored Bursts",
    "2D DNA",
    "2D DNA Spiral",
    "2D Drift",
    "2D Firenoise",
    "2D Frizzles",
    "2D Hypnotic",
    "2D Julia",
    "2D Lissajous",
    "2D Matrix",
    "2D Metaballs",
    "2D Noise",
    "2D Plasma Ball",
    "2D Polar Lights",
    "2D Pulser",
    "2D Sindots",
    "2D Squared Swirl",
    "2D Sun Radiation",
    "2D Tartan",
    "2D Waverly"
  };
  
  // Check by name if available
  if (effectName && strlen(effectName) > 0) {
    for (size_t i = 0; i < sizeof(noPaletteEffects) / sizeof(noPaletteEffects[0]); i++) {
      if (strcmp(effectName, noPaletteEffects[i]) == 0) {
        Serial.printf("[PALETTE] Effect '%s' ignores palettes\n", effectName);
        return true;
      }
    }
  }
  
  // Check by common effect IDs that are known to ignore palettes
  switch (effectId) {
    case 0:   // Solid
    case 1:   // Blink
    case 2:   // Breathe
    case 3:   // Wipe
    case 4:   // Wipe Random
    case 5:   // Random Colors
    case 6:   // Sweep
    case 7:   // Dynamic
    case 8:   // Colorloop
    case 9:   // Rainbow
    case 10:  // Rainbow Runner
    case 124: // Distortion Waves (from your log)
      Serial.printf("[PALETTE] Effect ID %d ignores palettes\n", effectId);
      return true;
    default:
      break;
  }
  
  return false;
}

// Color palette visualization - draw colored squares from segment colors
// Draw representative colors for known palettes
bool UIManager_drawPaletteRepresentativeColors(int paletteId, int startX, int startY) {
  // CRASH FIX: Verify display is ready
  if (!DisplayManager_isScreenOn()) {
    return false;
  }
  
  auto& tft = DisplayManager_getTFT();
  
  // Define representative colors for known palettes (RGB values converted to 565 format)
  uint16_t paletteColors[8];  // Max 8 colors
  int colorCount = 0;
  
  switch (paletteId) {
    case 22: // Beach palette - from WLED definition
      paletteColors[0] = tft.color565(0, 128, 255);   // Ocean blue
      paletteColors[1] = tft.color565(0, 255, 255);   // Cyan
      colorCount = 2;
      break;
      
    case 28: // Hult palette
      paletteColors[0] = tft.color565(255, 43, 0);    // Red-orange
      paletteColors[1] = tft.color565(255, 102, 0);   // Orange
      paletteColors[2] = tft.color565(255, 200, 0);   // Yellow-orange
      paletteColors[3] = tft.color565(255, 255, 0);   // Yellow
      paletteColors[4] = tft.color565(128, 255, 0);   // Yellow-green
      paletteColors[5] = tft.color565(0, 255, 128);   // Green-cyan
      paletteColors[6] = tft.color565(0, 128, 255);   // Cyan-blue
      paletteColors[7] = tft.color565(128, 0, 255);   // Blue-purple
      colorCount = 8;
      break;
      
    case 36: // Icefire palette - from WLED definition
      paletteColors[0] = tft.color565(0, 0, 255);     // Blue
      paletteColors[1] = tft.color565(255, 255, 0);   // Yellow
      paletteColors[2] = tft.color565(255, 0, 0);     // Red
      colorCount = 3;
      break;
      
    case 53: // C9 New palette 
      paletteColors[0] = tft.color565(255, 5, 0);     // Red
      paletteColors[1] = tft.color565(100, 57, 2);    // Orange-brown
      paletteColors[2] = tft.color565(6, 126, 2);     // Green
      paletteColors[3] = tft.color565(4, 30, 114);    // Blue
      colorCount = 4;
      break;
      
    case 58: // Toxy Reef palette 
      paletteColors[0] = tft.color565(0, 255, 255);   // Cyan
      paletteColors[1] = tft.color565(0, 200, 255);   // Light blue
      paletteColors[2] = tft.color565(0, 100, 255);   // Blue
      paletteColors[3] = tft.color565(100, 0, 255);   // Purple
      paletteColors[4] = tft.color565(255, 0, 200);   // Magenta
      paletteColors[5] = tft.color565(255, 100, 0);   // Orange
      colorCount = 6;
      break;
      
    default:
      return false; // Unknown palette
  }
  
  // Draw the color squares
  const int squareSize = 16;
  const int spacing = 2;
  int xPos = startX;
  
  for (int i = 0; i < colorCount; i++) {
    tft.fillRect(xPos, startY, squareSize, squareSize, paletteColors[i]);
    tft.drawRect(xPos, startY, squareSize, squareSize, ST77XX_WHITE); // Border
    xPos += squareSize + spacing;
    
    // Wrap to next row if needed
    if (xPos + squareSize > 220) {
      xPos = startX;
      startY += squareSize + spacing;
    }
  }
  
  Serial.printf("[COLOR] Drew %d representative colors for palette %d\n", colorCount, paletteId);
  return true;
}

// Draw instance dropdown at top of now playing page
void UIManager_drawInstanceDropdown() {
  auto& tft = DisplayManager_getTFT();
  
  // Dropdown configuration
  const int dropdownX = 10;
  const int dropdownY = 25;
  const int dropdownW = 220;
  const int dropdownH = 25;
  
  // Clear dropdown area
  tft.fillRect(dropdownX, dropdownY, dropdownW, dropdownH, ST77XX_BLACK);
  
  // Draw dropdown background
  tft.fillRect(dropdownX, dropdownY, dropdownW, dropdownH, ST77XX_BLACK);
  tft.drawRect(dropdownX, dropdownY, dropdownW, dropdownH, ST77XX_WHITE);
  
  // Get current instance info
  const char* currentName = safeGetWLEDName(CURRENT_WLED_INSTANCE);
  const char* currentIP = safeGetWLEDIP(CURRENT_WLED_INSTANCE);
  
  // Format display text (name if different from IP, otherwise just IP)
  char displayText[40];
  if (currentName && currentIP && strcmp(currentName, currentIP) != 0) {
    snprintf(displayText, sizeof(displayText), "%s", currentName);
  } else if (currentIP) {
    snprintf(displayText, sizeof(displayText), "%s", currentIP);
  } else {
    snprintf(displayText, sizeof(displayText), "Unknown");
  }
  
  // Draw current selection text
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(dropdownX + 5, dropdownY + 8);
  tft.print(displayText);
  
  // Draw dropdown arrow (simple ASCII)
  tft.setCursor(dropdownX + dropdownW - 15, dropdownY + 8);
  tft.print("v");
  
  // Store dropdown bounds for touch detection
  UIManager_dropdownBounds.x = dropdownX;
  UIManager_dropdownBounds.y = dropdownY;
  UIManager_dropdownBounds.w = dropdownW;
  UIManager_dropdownBounds.h = dropdownH;
  
  // If dropdown is expanded, draw the list
  if (UIManager_dropdownExpanded) {
    UIManager_drawDropdownList();
  }
}

// Draw expanded dropdown list
void UIManager_drawDropdownList() {
  auto& tft = DisplayManager_getTFT();
  
  const int listX = UIManager_dropdownBounds.x;
  const int listY = UIManager_dropdownBounds.y + UIManager_dropdownBounds.h;
  const int listW = UIManager_dropdownBounds.w;
  const int itemH = 30;
  const int maxItems = min(WLED_INSTANCE_COUNT, (uint8_t)6); // Max 6 items to fit on screen
  const int listH = itemH * maxItems;
  
  // Draw list background
  tft.fillRect(listX, listY, listW, listH, ST77XX_BLACK);
  tft.drawRect(listX, listY, listW, listH, ST77XX_WHITE);
  
  // Draw each instance
  for (uint8_t i = 0; i < maxItems; i++) {
    const int itemY = listY + (i * itemH);
    
    // Highlight current selection
    bool isSelected = (i == CURRENT_WLED_INSTANCE);
    uint16_t bgColor = isSelected ? ST77XX_BLUE : ST77XX_BLACK;
    uint16_t fgColor = isSelected ? ST77XX_WHITE : ST77XX_GREEN;
    
    // Draw item background
    tft.fillRect(listX + 1, itemY + 1, listW - 2, itemH - 2, bgColor);
    
    // Get instance info
    const char* name = safeGetWLEDName(i);
    const char* ip = safeGetWLEDIP(i);
    
    // Format display text
    char itemText[35];
    if (name && ip && strcmp(name, ip) != 0) {
      snprintf(itemText, sizeof(itemText), "%s", name);
    } else if (ip) {
      snprintf(itemText, sizeof(itemText), "%s", ip);
    } else {
      snprintf(itemText, sizeof(itemText), "Instance %d", i + 1);
    }
    
    // Draw item text
    tft.setTextSize(1);
    tft.setTextColor(fgColor, bgColor);
    tft.setCursor(listX + 5, itemY + 10);
    tft.print(itemText);
  }
}

// Unified Color Band Renderer - creates consistent full-width color bands
void UIManager_drawUnifiedColorBand(JsonObject seg, int startX, int startY, int paletteId = 0, int effectId = 0) {
  // CRASH FIX: Verify display is ready
  if (!DisplayManager_isScreenOn()) {
    return;
  }
  
  auto& tft = DisplayManager_getTFT();
  
  // Configuration for unified color band
  const int bandWidth = 220;      // Total width to fill (240px screen - 20px margins)
  const int bandHeight = 20;      // Height of the color band
  const int maxColors = 16;       // Maximum colors to display
  
  // Clear the area first
  tft.fillRect(startX, startY, bandWidth, bandHeight + 10, ST77XX_BLACK);
  
  // Try to get palette colors from WLED first (like PowerShell script)
  PaletteColorData paletteColors[maxColors];
  bool hasPaletteData = false;
  int colorCount = 0;
  
  if (paletteId > 0) {
    hasPaletteData = WLEDClient_fetchPaletteColors(paletteId, paletteColors, maxColors);
    if (hasPaletteData) {
      // Count valid palette colors
      for (int i = 0; i < maxColors; i++) {
        if (paletteColors[i].valid) colorCount++;
      }
      Serial.printf("[COLOR_BAND] Found %d palette colors for palette %d\n", colorCount, paletteId);
    }
  }
  
  // Fallback to segment colors if no palette data
  if (!hasPaletteData || colorCount == 0) {
    if (!seg || seg.isNull()) {
      tft.setTextSize(1);
      tft.setTextColor(0x8410, ST77XX_BLACK);  // Gray color
      tft.setCursor(startX, startY + 5);
      tft.print("No color data available");
      return;
    }
    
    JsonArray colors = seg["col"];
    if (!colors || colors.isNull() || colors.size() == 0) {
      tft.setTextSize(1);
      tft.setTextColor(0x8410, ST77XX_BLACK);  // Gray color
      tft.setCursor(startX, startY + 5);
      tft.print("No segment colors");
      return;
    }
    
    // Convert segment colors to our format
    colorCount = 0;
    for (size_t i = 0; i < colors.size() && colorCount < maxColors; i++) {
      JsonArray colorArray = colors[i];
      if (colorArray && !colorArray.isNull() && colorArray.size() >= 3) {
        int r = constrain((int)colorArray[0], 0, 255);
        int g = constrain((int)colorArray[1], 0, 255);
        int b = constrain((int)colorArray[2], 0, 255);
        
        // Skip pure black colors
        if (!(r == 0 && g == 0 && b == 0)) {
          paletteColors[colorCount].r = r;
          paletteColors[colorCount].g = g;
          paletteColors[colorCount].b = b;
          paletteColors[colorCount].valid = true;
          colorCount++;
        }
      }
    }
    Serial.printf("[COLOR_BAND] Using %d segment colors\n", colorCount);
  }
  
  // If still no colors, show placeholder
  if (colorCount == 0) {
    tft.setTextSize(1);
    tft.setTextColor(0x8410, ST77XX_BLACK);  // Gray color
    tft.setCursor(startX, startY + 5);
    tft.print("No valid colors found");
    return;
  }
  
  // Calculate segment width for even distribution
  int segmentWidth = bandWidth / colorCount;
  int remainder = bandWidth % colorCount;
  
  Serial.printf("[COLOR_BAND] Drawing %d colors with segment width %d (remainder: %d)\n", 
                colorCount, segmentWidth, remainder);
  
  // Draw unified color band
  int currentX = startX;
  int colorIndex = 0;
  
  for (int i = 0; i < maxColors && colorIndex < colorCount; i++) {
    if (!paletteColors[i].valid) continue;
    
    // Calculate this segment's width (distribute remainder across first segments)
    int thisSegmentWidth = segmentWidth + (colorIndex < remainder ? 1 : 0);
    
    // Convert to 565 color format
    uint16_t color565 = ((paletteColors[i].r & 0xF8) << 8) | 
                        ((paletteColors[i].g & 0xFC) << 3) | 
                        (paletteColors[i].b >> 3);
    
    // Draw filled rectangle for this color segment
    tft.fillRect(currentX, startY, thisSegmentWidth, bandHeight, color565);
    
    Serial.printf("[COLOR_BAND] Segment %d: x=%d width=%d RGB(%d,%d,%d) color=0x%04X\n", 
                  colorIndex, currentX, thisSegmentWidth, 
                  paletteColors[i].r, paletteColors[i].g, paletteColors[i].b, color565);
    
    currentX += thisSegmentWidth;
    colorIndex++;
  }
  
  // Draw border around the entire band for definition
  tft.drawRect(startX, startY, bandWidth, bandHeight, ST77XX_WHITE);
  
  // Show color count below the band
  tft.setTextSize(1);
  tft.setTextColor(0x8410, ST77XX_BLACK);  // Gray color
  tft.setCursor(startX, startY + bandHeight + 2);
  if (hasPaletteData && paletteId > 0) {
    tft.printf("Palette colors (%d)", colorCount);
  } else {
    tft.printf("Segment colors (%d)", colorCount);
  }
}

// Legacy function for compatibility - now calls unified renderer
void UIManager_drawColorPalette(JsonObject seg, int startX, int startY, int paletteId = 0, int effectId = 0) {
  // Simply call the new unified color band renderer
  UIManager_drawUnifiedColorBand(seg, startX, startY, paletteId, effectId);
}

// Enhanced WLED state information with playlist and color palette support
void UIManager_drawEnhancedWLEDStateInfo(JsonDocument& doc) {
  // CRASH FIX: Verify display is ready
  if (!DisplayManager_isScreenOn()) {
    Serial.println("[UI] Cannot draw WLED state - screen is off");
    return;
  }
  
  auto& tft = DisplayManager_getTFT();
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  
  int yPos = 60;  // Start lower to account for dropdown
  const int lineHeight = 18;
  
  // Skip instance name line since it's now in the dropdown at the top
  
  JsonObject state = doc["state"];
  if (!state || state.isNull()) {
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.setCursor(10, yPos);
    tft.print("No state data available");
    return;
  }
  
  // Get main segment
  int mainSeg = state["mainseg"] | 0;
  JsonArray segments = state["seg"];
  JsonObject seg;
  
  Serial.printf("[WLED] Main segment: %d, Total segments: %d\n", mainSeg, segments ? segments.size() : 0);
  
  if (segments && !segments.isNull() && segments.size() > mainSeg) {
    seg = segments[mainSeg];
    Serial.printf("[WLED] Using main segment %d\n", mainSeg);
  } else if (segments && !segments.isNull() && segments.size() > 0) {
    seg = segments[0];
    Serial.printf("[WLED] Using first segment (main segment %d not available)\n", mainSeg);
  } else {
    Serial.println("[WLED] No segments available");
  }
  
  // Extract IDs - handle playlist ID specially to preserve -1
  int presetId = state["ps"] | 0;
  int playlistId = -1;  // Default to -1 (no playlist)  
  if (!state["pl"].isNull()) {
    playlistId = state["pl"];
  }
  int effectId = seg["fx"] | 0;
  int paletteId = seg["pal"] | 0;
  
  Serial.printf("[DEBUG] Current IDs: preset=%d, playlist=%d, effect=%d, palette=%d\n", 
                presetId, playlistId, effectId, paletteId);
  
  // Line 2: Playlist/Preset information
  tft.setCursor(10, yPos);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  
  // Check for playlist first - only if playlist ID > 0 or try to detect from preset
  char playlistName[32];
  int detectedPlaylistId = 0;
  bool hasPlaylist = false;
  
  if (playlistId > 0) {
    // Use provided playlist ID from state
    hasPlaylist = WLEDClient_fetchPresetName(playlistId, playlistName, sizeof(playlistName));
    detectedPlaylistId = playlistId;
    Serial.printf("[PLAYLIST] Using explicit playlist ID %d\n", playlistId);
  } else if (playlistId == -1 || playlistId == 0) {
    // Only try to detect playlist from current preset if no explicit playlist ID
    hasPlaylist = WLEDClient_detectPlaylist(presetId, playlistName, sizeof(playlistName), &detectedPlaylistId);
    if (hasPlaylist) {
      Serial.printf("[PLAYLIST] Detected playlist from preset %d\n", presetId);
    }
  }
  
  // Only show preset/playlist information if we have valid IDs
  if (hasPlaylist && presetId > 0) {
    // Get actual preset name
    char presetName[32];
    if (!UIManager_getActualPresetName(presetId, presetName, sizeof(presetName))) {
      snprintf(presetName, sizeof(presetName), "Preset %d", presetId);
    }
    
    tft.printf("%s (%d) | %s (%d)", playlistName, detectedPlaylistId, presetName, presetId);
  } else if (presetId > 0) {
    // Just show preset information with actual names and IDs
    char presetName[32];
    bool foundInQuickLoads = false;
    const char* quickLoadName = nullptr;
    
    // Check if this preset matches any QuickLoad slot
    for (uint8_t ql = 0; ql < 6; ql++) {
      if (WLEDClient_getQuickLoadPreset(ql) == presetId) {
        switch (ql) {
          case 0: quickLoadName = "QL1"; break;
          case 1: quickLoadName = "QL2"; break;
          case 2: quickLoadName = "QL3"; break;
          case 3: quickLoadName = "QL4"; break;
          case 4: quickLoadName = "QL5"; break;
          case 5: quickLoadName = "QL6"; break;
        }
        foundInQuickLoads = true;
        break;
      }
    }
    
    if (foundInQuickLoads && quickLoadName) {
      // For QuickLoad slots, still try to get the actual preset name
      if (UIManager_getActualPresetName(presetId, presetName, sizeof(presetName))) {
        tft.printf("%s (%d)", presetName, presetId);
      } else {
        tft.printf("%s (%d)", quickLoadName, presetId);
      }
    } else {
      // Try to get actual preset name
      if (UIManager_getActualPresetName(presetId, presetName, sizeof(presetName))) {
        tft.printf("%s (%d)", presetName, presetId);
      } else {
        tft.printf("Preset %d", presetId);
      }
    }
  } else {
    // Don't display anything when preset ID is 0 or -1
    tft.print("(No preset/playlist active)");
  }
  yPos += lineHeight;
  
  // Line 3: Effect name - get actual name from WLED
  tft.setCursor(10, yPos);
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  char effectName[32];
  if (UIManager_getWLEDEffectName(effectId, effectName, sizeof(effectName))) {
    tft.print(effectName);
  } else {
    tft.printf("Effect %d", effectId);
  }
  yPos += lineHeight;
  
  // Line 4: Palette name - get actual name from WLED
  tft.setCursor(10, yPos);
  tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
  char paletteName[32];
  if (UIManager_getWLEDPaletteName(paletteId, paletteName, sizeof(paletteName))) {
    tft.print(paletteName);
  } else {
    tft.printf("Palette %d", paletteId);
  }
  yPos += lineHeight + 5;
  
  // Extract and display color palette as colored squares
  UIManager_drawColorPalette(seg, 10, yPos, paletteId, effectId);
  
  // Refresh button at bottom
  yPos = 280;
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.drawRect(80, yPos, 80, 30, ST77XX_WHITE);
  tft.setCursor(95, yPos + 8);
  tft.print("Refresh");
}

// Keep the original function for compatibility
void UIManager_drawWLEDStateInfo(JsonDocument& doc) {
  auto& tft = DisplayManager_getTFT();
  
  tft.setTextSize(2); // Increased from 1 to 2 for better readability
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  
  int yPos = 70;
  const int lineHeight = 25; // Increased for larger font
  
  // Line 1: WLED Instance name
  tft.setCursor(10, yPos);
  tft.print("Instance:");
  const char* instanceName = safeGetWLEDName(CURRENT_WLED_INSTANCE);
  tft.setCursor(10, yPos + lineHeight);
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.print(instanceName);
  yPos += lineHeight * 2;
  
  // Line 2: Current preset number and name
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.print("Preset:");
  
  JsonObject state = doc["state"];
  if (state && !state.isNull()) {
    int presetId = state["ps"] | -1;
    if (presetId >= 0) { // Changed from > 0 to >= 0 to handle preset 0
      tft.setCursor(10, yPos + lineHeight);
      tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
      
      // Try to get preset name from WLED /json/presets endpoint
      const char* presetName = UIManager_getPresetName(presetId);
      if (presetName && strlen(presetName) > 0) {
        tft.printf("%d: %s", presetId, presetName);
      } else {
        // Check if this preset matches any QuickLoad slot as fallback
        const char* qlName = nullptr;
        for (uint8_t ql = 0; ql < 6; ql++) {
          if (WLEDClient_getQuickLoadPreset(ql) == presetId) {
            switch (ql) {
              case 0: qlName = "QL1"; break;
              case 1: qlName = "QL2"; break;
              case 2: qlName = "QL3"; break;
              case 3: qlName = "QL4"; break;
              case 4: qlName = "QL5"; break;
              case 5: qlName = "QL6"; break;
            }
            break;
          }
        }
        if (qlName) {
          tft.printf("%d (%s)", presetId, qlName);
        } else {
          tft.printf("%d", presetId);
        }
      }
    } else {
      tft.setCursor(10, yPos + lineHeight);
      tft.setTextColor(0x8410, ST77XX_BLACK);  // Gray color
      tft.print("None active");
    }
  }
  yPos += lineHeight * 2;
  
  // Line 3: Current effect name (no ID, just name)
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.print("Effect:");
  
  JsonArray seg = state["seg"];
  if (seg && !seg.isNull() && seg.size() > 0) {
    JsonObject firstSeg = seg[0];
    int effectId = firstSeg["fx"] | 0;
    
    // Get effect name (no ID display)
    const char* effectName = UIManager_getEffectName(effectId);
    tft.setCursor(10, yPos + lineHeight);
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    if (effectName && strlen(effectName) > 0) {
      tft.print(effectName);
    } else {
      tft.printf("Unknown (%d)", effectId); // Only show ID if name lookup fails
    }
  }
  yPos += lineHeight * 2;
  
  // Line 4: Current color palette name (no ID, just name)
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.print("Palette:");
  
  if (seg && !seg.isNull() && seg.size() > 0) {
    JsonObject firstSeg = seg[0];
    int paletteId = firstSeg["pal"] | 0;
    
    // Get palette name (no ID display)
    const char* paletteName = UIManager_getPaletteName(paletteId);
    tft.setCursor(10, yPos + lineHeight);
    tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
    if (paletteName && strlen(paletteName) > 0) {
      tft.print(paletteName);
    } else {
      tft.printf("Unknown (%d)", paletteId); // Only show ID if name lookup fails
    }
  }
  yPos += lineHeight * 2.5;
  
  // Refresh button
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.drawRect(80, yPos, 80, 30, ST77XX_WHITE);
  tft.setCursor(95, yPos + 8);
  tft.print("Refresh");
}

// Handle touch events on Now Playing page
void UIManager_handleNowPlayingTouch(int16_t x, int16_t y) {
  // Check if touch is within dropdown bounds
  if (x >= UIManager_dropdownBounds.x && 
      x <= (UIManager_dropdownBounds.x + UIManager_dropdownBounds.w) &&
      y >= UIManager_dropdownBounds.y && 
      y <= (UIManager_dropdownBounds.y + UIManager_dropdownBounds.h)) {
    
    Serial.println("[UI] Dropdown touched - toggling expansion");
    UIManager_dropdownExpanded = !UIManager_dropdownExpanded;
    UIManager_needsFullRepaint = true;
    return;
  }
  
  // Check if dropdown is expanded and touch is within list area
  if (UIManager_dropdownExpanded) {
    const int listY = UIManager_dropdownBounds.y + UIManager_dropdownBounds.h;
    const int itemH = 30;
    const int maxItems = min(WLED_INSTANCE_COUNT, (uint8_t)6);
    const int listH = itemH * maxItems;
    
    if (x >= UIManager_dropdownBounds.x && 
        x <= (UIManager_dropdownBounds.x + UIManager_dropdownBounds.w) &&
        y >= listY && y <= (listY + listH)) {
      
      // Calculate which item was touched
      int itemIndex = (y - listY) / itemH;
      if (itemIndex >= 0 && itemIndex < maxItems && itemIndex < WLED_INSTANCE_COUNT) {
        Serial.printf("[UI] Dropdown item %d selected\n", itemIndex);
        
        // Switch to selected instance if different
        if (itemIndex != CURRENT_WLED_INSTANCE) {
          const char* friendlyName = safeGetWLEDName(itemIndex);
          const char* ipAddress = safeGetWLEDIP(itemIndex);
          
          // Reset WLED client state when switching instances  
          WLEDClient_forceResetBackoff();
          WLEDClient_forceResetHTTPState();
          Serial.println("[UI] Reset WLED client backoff and HTTP state for instance switch");
          
          // Use persistence manager to save the change
          if (PersistenceManager_onWLEDInstanceChanged(itemIndex)) {
            Serial.printf("[UI] Switched to WLED instance %d: %s (%s) - SAVED\n", 
                          itemIndex, friendlyName, ipAddress);
          } else {
            // Fallback if persistence fails
            CURRENT_WLED_INSTANCE = itemIndex;
            Serial.printf("[UI] Switched to WLED instance %d: %s (%s) - NOT SAVED\n", 
                          itemIndex, friendlyName, ipAddress);
          }
        }
        
        // Collapse dropdown and refresh display
        UIManager_dropdownExpanded = false;
        UIManager_needsFullRepaint = true;
        return;
      }
    } else {
      // Touch outside dropdown list - collapse it
      UIManager_dropdownExpanded = false;
      UIManager_needsFullRepaint = true;
      return;
    }
  }
  
  // Check if touch is within refresh button bounds (80, 280, 80x30)
  int buttonY = 280;
  
  if (x >= 80 && x <= 160 && y >= buttonY && y <= (buttonY + 30)) {
    Serial.println("[UI] Refresh button pressed on Now Playing page");
    
    // Aggressive connection reset sequence
    WLEDClient_forceResetBackoff();
    WLEDClient_forceResetHTTPState();
    Serial.println("[UI] Reset WLED client backoff and HTTP state for immediate retry");
    
    // Show immediate feedback
    auto& tft = DisplayManager_getTFT();
    tft.fillRect(10, 60, 220, 200, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setCursor(10, 80);
    tft.print("Refreshing connection...");
    
    // Small delay to show the message
    delay(100);
    
    // Force a refresh of the now playing page by triggering a repaint
    UIManager_needsFullRepaint = true;
    
    // Optional: Add visual feedback for button press
    tft.fillRect(80, buttonY, 80, 30, ST77XX_WHITE);
    tft.setTextColor(ST77XX_BLACK, ST77XX_WHITE);
    tft.setCursor(95, buttonY + 8);
    tft.setTextSize(1);
    tft.print("Refresh");
    
    delay(100); // Brief visual feedback
    UIManager_needsFullRepaint = true; // Trigger immediate repaint
  }
}

#endif // ENABLE_NOW_PLAYING_PAGE

// OPTIMIZED: Smart page painting with minimal redraws
void UIManager_paintPage() {
  // CRITICAL FIX: Update activity on every paint to prevent timeout during active UI use
  DisplayManager_updateActivity();
  
  Serial.printf("[UI] paintPage() start - screen on=%s\n", DisplayManager_isScreenOn() ? "Y" : "N");
  
  if (!DisplayManager_isScreenOn()) {
    Serial.println("[UI] paintPage() aborted - screen is off");
    return;
  }
  
  Serial.println("[UI] Getting TFT reference...");
  
  // CRASH FIX: Double-check that display is truly ready before any TFT operations
  // This prevents crashes when TFT object is not properly initialized
  if (!DisplayManager_isScreenOn()) {
    Serial.println("[UI] Display not ready - aborting paint");
    return;
  }
  
  auto& tft = DisplayManager_getTFT();
  
  // CRITICAL FIX: Use direct TFT operations instead of DisplayManager functions during early boot
  if (UIManager_needsFullRepaint) {
    Serial.println("[UI] Clearing screen (direct TFT access)...");
    
    // Use direct TFT operations to avoid potential DisplayManager issues during boot
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[UI] Direct screen clear complete");
    
    // Only skip battery drawing during early boot mode
    if (!UIManager_earlyBootMode) {
      DisplayManager_drawBatteryStatus();
    } else {
      Serial.println("[UI] Skipping battery status during early boot");
    }
    
    UIManager_needsFullRepaint = false;
    Serial.println("[UI] Screen initialization complete");
  }
  
  // Map logical page to physical page
  int physicalPage = UIManager_mapToPhysicalPage(UIManager_currentPage);
  Serial.printf("[UI] Drawing page %d (logical=%d)\n", physicalPage, UIManager_currentPage);
  
  switch (physicalPage) {
    case 0:
      Serial.printf("[UI] Drawing %d buttons for page 0...\n", UIManager_PAGE0_BTN_COUNT);
      for (int i = 0; i < UIManager_PAGE0_BTN_COUNT; i++) {
        Serial.printf("[UI] Drawing button %d...\n", i);
        UIManager_drawIconButton(UIManager_page0Buttons[i], false);
        Serial.printf("[UI] Button %d complete\n", i);
      }
      Serial.println("[UI] Page 0 complete");
      break;
      
    case 1:
      for (int i = 0; i < UIManager_PAGE1_BTN_COUNT; i++) {
        UIManager_drawIconButton(UIManager_page1Buttons[i], false);
      }
      break;

#if ENABLE_NOW_PLAYING_PAGE      
    case 2: {
      Serial.println("[UI] Drawing now playing page...");
      UIManager_drawNowPlayingPage();
      Serial.println("[UI] Now playing page complete");
      break;
    }
#endif // ENABLE_NOW_PLAYING_PAGE
      
#if ENABLE_BRIGHTNESS_PAGE
    case 3: {
      // FIXED: Always fetch fresh brightness and draw slider
      Serial.println("[UI] Drawing brightness page...");
      
      // Fetch current brightness from WLED
      int bri = WLEDClient_fetchBrightness();
      if (bri >= 0) {
        UIManager_brightnessSlider.value = constrain(bri, 0, 255);
        Serial.printf("[UI] Fetched brightness: %d\n", bri);
        
        // Sync the Twist manager with the current brightness
        if (TwistManager_isAvailable()) {
          TwistManager_setBrightness(bri);
        }
      } else {
        // Use last known value or default if fetch fails
        if (UIManager_brightnessSlider.value <= 0) {
          UIManager_brightnessSlider.value = 128; // Default to 50%
        }
        Serial.printf("[UI] Using cached/default brightness: %d\n", UIManager_brightnessSlider.value);
      }
      
      // Always draw the slider
      UIManager_drawBrightnessSlider();
      break;
    }
#endif // ENABLE_BRIGHTNESS_PAGE
    
    case 3:
      // System page - now on page 3 since now playing is enabled on page 2
      for (int i = 0; i < UIManager_PAGE3_BTN_COUNT; i++) {
        UIManager_drawIconButton(UIManager_page3Buttons[i], false);
      }
      break;
      
    // WLED selection page removed - functionality integrated into now playing page
  }
}

void UIManager_navToPage(int newPage) {
  int wrapped = (newPage % TOTAL_UI_PAGES + TOTAL_UI_PAGES) % TOTAL_UI_PAGES;
  if (wrapped != UIManager_currentPage) {
    UIManager_currentPage = wrapped;
    UIManager_needsFullRepaint = true; // Force full repaint on page change
    UIManager_lastBrightnessValue = -1; // Force slider redraw
    UIManager_paintPage();
    
    // Reset Now Playing state tracking when navigating to that page
    if (UIManager_mapToPhysicalPage(wrapped) == 2) { // Now Playing page
      UIManager_forceNowPlayingUpdate = true;
      Serial.println("[UI] Navigated to Now Playing - will force update");
    }
    
    Serial.printf("[UI] Navigated to logical page %d (physical page %d)\n", 
                  UIManager_currentPage, UIManager_mapToPhysicalPage(UIManager_currentPage));
    
    if (WiFiManager_isConnected()) {
      bool ok = WLEDClient_initQuickLoads();
      if (!ok) {
        Serial.println("[WLED] QuickLoads refresh failed (see logs); using existing mapping");
      }
    }
  }
}

// FIXED: Enhanced swipe navigation with touch suppression
void UIManager_handleSwipeNavigation(int16_t x, int16_t y) {
  int16_t dx = x - UIManager_touchStartX;
  int16_t dy = y - UIManager_touchStartY;
  uint32_t dt = millis() - UIManager_touchStartTime;

  if (dt < SWIPE_TIMEOUT_MS && abs(dx) > SWIPE_THRESHOLD && abs(dy) < VERTICAL_TOLERANCE) {
    // FIXED: Mark swipe as detected and suppress subsequent touch events
    UIManager_swipeDetected = true;
    UIManager_lastSwipeTime = millis();
    
    if (dx > 0) UIManager_navToPage(UIManager_currentPage - 1);
    else        UIManager_navToPage(UIManager_currentPage + 1);
    
    Serial.printf("[UI] Swipe detected (dx=%d), suppressing touches for %dms\n", 
                  dx, SWIPE_SUPPRESS_TIME_MS);
  }
}

// OPTIMIZED: Non-blocking button press feedback with queued commands
void UIManager_handlePage0Touch(int16_t x, int16_t y) {
  // FIXED: Don't process touches if swipe was detected recently
  if (UIManager_swipeDetected) {
    Serial.println("[UI] Touch suppressed - recent swipe detected");
    return;
  }
  
  for (int i = 0; i < UIManager_PAGE0_BTN_COUNT; i++) {
    if (UIManager_hitTest(UIManager_page0Buttons[i], x, y)) {
      // CRITICAL FIX: Update activity on button press to prevent timeout
      DisplayManager_updateActivity();
      
      // OPTIMIZED: Immediate visual feedback - don't wait for network
      UIManager_drawIconButton(UIManager_page0Buttons[i], true); 
      UIManager_pressedButtonIndex = i;
      UIManager_buttonPressTime = millis();
      
      // ENHANCED: Use fixed quick launch presets for consistent operation
      uint8_t a = UIManager_page0Buttons[i].action;
      bool queued = false;
      
      switch (a) {
        case ACTION_QUICK_LAUNCH_1:
          queued = NetworkTask_queueFixedQuickLaunch1();
          break;
        case ACTION_QUICK_LAUNCH_2:
          queued = NetworkTask_queueFixedQuickLaunch2();
          break;
        case ACTION_QUICK_LAUNCH_3:
          queued = NetworkTask_queueFixedQuickLaunch3();
          break;
        case ACTION_QUICK_LAUNCH_4:
          queued = NetworkTask_queueFixedQuickLaunch4();
          break;
        case ACTION_QUICK_LAUNCH_5:
          queued = NetworkTask_queueFixedQuickLaunch5();
          break;
        case ACTION_QUICK_LAUNCH_6:
          queued = NetworkTask_queueFixedQuickLaunch6();
          break;
        default:
          // Fallback for any other actions
          queued = NetworkTask_queuePreset(a);
          break;
      }
      
      if (!queued) {
        Serial.printf("[UI] Failed to queue fixed preset action %u\n", a);
      }
      
      Serial.printf("[UI] Queued action %u (non-blocking)\n", a);
      return;
    }
  }
}

void UIManager_handlePage1Touch(int16_t x, int16_t y) {
  // FIXED: Don't process touches if swipe was detected recently
  if (UIManager_swipeDetected) {
    Serial.println("[UI] Touch suppressed - recent swipe detected");
    return;
  }
  
  for (int i = 0; i < UIManager_PAGE1_BTN_COUNT; i++) {
    if (UIManager_hitTest(UIManager_page1Buttons[i], x, y)) {
      // CRITICAL FIX: Update activity on button press to prevent timeout
      DisplayManager_updateActivity();
      
      // OPTIMIZED: Immediate visual feedback
      UIManager_drawIconButton(UIManager_page1Buttons[i], true);
      
      UIManager_pressedButtonIndex = i + 100; // Offset to avoid conflicts
      UIManager_buttonPressTime = millis();
      
      // PHASE 2B: Use network task for all commands
      bool queued = true;
      switch (UIManager_page1Buttons[i].action) {
        case 10: queued = NetworkTask_queuePowerToggle();      break;
        case 11: queued = NetworkTask_queuePresetCycle(true);  break;
        case 12: queued = NetworkTask_queuePresetCycle(false); break;
        case 13: queued = NetworkTask_queuePaletteCycle(true); break;
        case 14: queued = NetworkTask_queuePaletteCycle(false);break;
      }
      if (!queued) Serial.printf("[UI] Failed to queue page1 action %u\n", UIManager_page1Buttons[i].action);
      return;
    }
  }
}

#if ENABLE_BRIGHTNESS_PAGE
void UIManager_handlePage2Touch(int16_t x, int16_t y) {
  // FIXED: Don't process touches if swipe was detected recently
  if (UIManager_swipeDetected) {
    Serial.println("[UI] Touch suppressed - recent swipe detected");
    return;
  }
  
  if (UIManager_inSliderActiveLane(x, y)) {
    // FIXED: Calculate brightness from Y position correctly
    int16_t clampedY = constrain(y, UIManager_brightnessSlider.y, 
                                UIManager_brightnessSlider.y + UIManager_brightnessSlider.h - 1);
    int16_t pos = clampedY - UIManager_brightnessSlider.y;
    int16_t newVal = 255 - (pos * 255 / (UIManager_brightnessSlider.h - 1)); // Inverted: top=255, bottom=0
    newVal = constrain(newVal, 1, 255); // Minimum brightness = 1
    
    UIManager_brightnessSlider.value = newVal;
    UIManager_drawBrightnessSlider(); // Immediate visual update
    
    // PHASE 2B: Queue brightness command via network task
    NetworkTask_queueBrightness((uint8_t)newVal);
    Serial.printf("[UI] Brightness set to %d (queued via network task)\n", newVal);
  }
}
#endif // ENABLE_BRIGHTNESS_PAGE

void UIManager_handlePage3Touch(int16_t x, int16_t y) {
  // FIXED: Don't process touches if swipe was detected recently
  if (UIManager_swipeDetected) {
    Serial.println("[UI] Touch suppressed - recent swipe detected");
    return;
  }
  
  for (int i = 0; i < UIManager_PAGE3_BTN_COUNT; i++) {
    if (UIManager_hitTest(UIManager_page3Buttons[i], x, y)) {
      UIManager_drawIconButton(UIManager_page3Buttons[i], true); 
      delay(150); // Keep delay for system actions
      UIManager_drawIconButton(UIManager_page3Buttons[i], false);
      
      if (UIManager_page3Buttons[i].action == 201) {
        auto& tft = DisplayManager_getTFT();
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(20, 140); 
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK); 
        tft.setTextSize(2);
        tft.print("Shutting down...");
        NetworkTask_cleanup(); // Clean up network task
        delay(250);
        PowerManager_shutdown();
      } else if (UIManager_page3Buttons[i].action == 202) {
        auto& tft = DisplayManager_getTFT();
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(60, 140); 
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK); 
        tft.setTextSize(2);
        tft.print("Restarting...");
        NetworkTask_cleanup(); // Clean up network task
        delay(200);
        PowerManager_restart();
      }
      return;
    }
  }
}

// FIXED: Enhanced touch handling with swipe detection and suppression
void UIManager_handleTouch(int16_t x, int16_t y, bool isPress, bool isRelease) {
  DisplayManager_updateActivity();
  if (!DisplayManager_isScreenOn()) return;

  // FIXED: Check if we should suppress touches due to recent swipe
  uint32_t now = millis();
  if (UIManager_swipeDetected && (now - UIManager_lastSwipeTime < SWIPE_SUPPRESS_TIME_MS)) {
    // Still in suppression period - ignore all touches
    return;
  } else if (UIManager_swipeDetected && (now - UIManager_lastSwipeTime >= SWIPE_SUPPRESS_TIME_MS)) {
    // Suppression period ended - clear the flag
    UIManager_swipeDetected = false;
    Serial.println("[UI] Touch suppression ended");
  }

  if (isPress) {
    UIManager_touchStartX = x; 
    UIManager_touchStartY = y; 
    UIManager_touchStartTime = now; 
    UIManager_touchMoved = false;
    UIManager_swipeDetected = false; // Reset swipe detection on new press
    return;
  }
  
  if (isRelease) {
    // Check for swipe first - this may set UIManager_swipeDetected
    UIManager_handleSwipeNavigation(x, y);
    
    // FIXED: Only handle button presses if no swipe was detected
    if (!UIManager_swipeDetected) {
      // Handle taps by logical page, then map to physical page for processing
      int physicalPage = UIManager_mapToPhysicalPage(UIManager_currentPage);
      
      if (physicalPage == 0) {
        UIManager_handlePage0Touch(x, y);
      } else if (physicalPage == 1) {
        UIManager_handlePage1Touch(x, y);
#if ENABLE_NOW_PLAYING_PAGE
      } else if (physicalPage == 2) {
        UIManager_handleNowPlayingTouch(x, y);  // Handle refresh button
#endif
#if ENABLE_BRIGHTNESS_PAGE
      } else if (physicalPage == 3) {
        UIManager_handlePage2Touch(x, y);  // Brightness slider
#endif
      } else if (physicalPage == 3) {
        UIManager_handlePage3Touch(x, y);  // System buttons
      // WLED selection touch handling removed - integrated into now playing page
      }
    }
    return;
  }

#if ENABLE_BRIGHTNESS_PAGE
  // FIXED: Handle drag/hold for brightness slider (only if not suppressed and enabled)
  if (!UIManager_swipeDetected && UIManager_mapToPhysicalPage(UIManager_currentPage) == 3) {
    if (UIManager_inSliderActiveLane(x, y)) {
      int16_t clampedY = constrain(y, UIManager_brightnessSlider.y, UIManager_brightnessSlider.y + UIManager_brightnessSlider.h - 1);
      int16_t pos = clampedY - UIManager_brightnessSlider.y;
      int16_t newVal = 255 - (pos * 255 / (UIManager_brightnessSlider.h - 1)); 
      newVal = constrain(newVal, 0, 255);
      
      if (abs(newVal - UIManager_brightnessSlider.value) > 1) {
        UIManager_brightnessSlider.value = newVal;
        UIManager_drawBrightnessSlider(); // This now uses smart updates
        UIManager_touchMoved = true;
      }
    }
  }
#endif // ENABLE_BRIGHTNESS_PAGE
}

#if ENABLE_WLED_SELECTION_PAGE
// WLED Selection Page - Display list of WLED instances with friendly names
void UIManager_drawWLEDSelectionPage() {
  auto& tft = DisplayManager_getTFT();
  
  // Clear the page area first
  tft.fillRect(0, 20, 240, 300, ST77XX_BLACK);
  
  // Draw header with better visibility and larger font
  tft.setTextSize(3); // Increased from 2 to 3 for better readability
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setCursor(10, 25); // Adjusted Y position for larger font
  tft.print("WLED Devices:");
  
  Serial.printf("[UI] Drawing WLED instances, count: %d\n", WLED_INSTANCE_COUNT);
  
  // Draw current selection indicator with better visibility  
  tft.setTextSize(2); // Increased from 1 to 2 for better readability
  tft.setCursor(10, 60); // Adjusted Y position for larger font and header
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  
  Serial.printf("[UI] Current instance: %d of %d\n", CURRENT_WLED_INSTANCE, WLED_INSTANCE_COUNT);
  
  if (CURRENT_WLED_INSTANCE < WLED_INSTANCE_COUNT && WLED_INSTANCE_COUNT > 0) {
    // Use safe access function
    const char* currentName = safeGetWLEDName(CURRENT_WLED_INSTANCE);
    tft.printf("Current: %s", currentName ? currentName : "Unknown");
    Serial.printf("[UI] Current name: %s\n", currentName ? currentName : "NULL");
  } else {
    tft.print("Current: Invalid");
    Serial.println("[UI] Current instance is invalid");
  }
  
  // Draw instance list - max 5 instances to fit on screen with larger fonts
  uint8_t maxInstances = min(WLED_INSTANCE_COUNT, (uint8_t)5); // Reduced from 6 to 5 for larger fonts
  Serial.printf("[UI] Drawing %d instances\n", maxInstances);
  
  for (uint8_t i = 0; i < maxInstances; i++) {
    int16_t y = 95 + (i * 35);  // More space between items for larger fonts
    
    Serial.printf("[UI] Drawing instance %d at y=%d\n", i, y);
    
    // Highlight current selection
    bool isSelected = (i == CURRENT_WLED_INSTANCE);
    uint16_t bgColor = isSelected ? ST77XX_WHITE : ST77XX_BLACK;
    uint16_t fgColor = isSelected ? ST77XX_BLACK : ST77XX_WHITE;
    
    // Draw selection background (larger for bigger text)
    tft.fillRect(5, y - 2, 230, 30, bgColor); // Increased height from 25 to 30
    tft.drawRect(5, y - 2, 230, 30, ST77XX_WHITE);
    
    // Draw instance info with safety checks and larger font
    tft.setTextSize(2); // Increased from 1 to 2 for better readability
    tft.setTextColor(fgColor, bgColor);
    tft.setCursor(10, y + 5);
    
    // Use safe access functions
    const char* displayName = safeGetWLEDName(i);
    const char* ipAddress = safeGetWLEDIP(i);
    
    Serial.printf("[UI] Instance %d: name='%s' ip='%s'\n", i, displayName ? displayName : "NULL", ipAddress ? ipAddress : "NULL");
    
    // Simple display - just show the name/IP
    if (displayName && ipAddress && strcmp(displayName, ipAddress) != 0) {
      // Show name and IP on separate lines
      tft.printf("%d: %s", i + 1, displayName);
      tft.setCursor(15, y + 15);
      tft.setTextColor(fgColor, bgColor);
      tft.printf("(%s)", ipAddress);
    } else if (ipAddress) {
      // Just show IP
      tft.printf("%d: %s", i + 1, ipAddress);
    } else {
      // Fallback
      tft.printf("%d: Unknown", i + 1);
    }
  }
  
  // Instructions with better visibility and larger font
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(2); // Increased from 1 to 2 for better readability
  tft.setCursor(10, 280); // Adjusted Y position for larger font
  tft.print("Touch device to select");
}

// Handle touch events on WLED selection page
void UIManager_handleWLEDSelectionTouch(int16_t x, int16_t y) {
  // Check if touch is within device list area
  uint8_t maxInstances = min(WLED_INSTANCE_COUNT, (uint8_t)5); // Updated to match display logic
  
  for (uint8_t i = 0; i < maxInstances; i++) {
    int16_t itemY = 95 + (i * 35);  // Match the new layout with larger spacing
    
    // Check if touch is within this item's bounds (updated for larger items)
    if (x >= 5 && x <= 235 && y >= (itemY - 2) && y <= (itemY + 28)) { // Updated height from 23 to 28
      // SAFETY: Check array bounds before accessing
      if (i >= WLED_INSTANCE_COUNT) {
        Serial.printf("[UI] Invalid WLED instance index: %d\n", i);
        break;
      }
      
      if (i != CURRENT_WLED_INSTANCE) {
        // ENHANCED: Switch to selected instance and save persistently
        const char* friendlyName = WLED_INSTANCES[i].friendlyName ? WLED_INSTANCES[i].friendlyName : "Unknown";
        const char* ipAddress = WLED_INSTANCES[i].ip ? WLED_INSTANCES[i].ip : "Unknown";
        
        if (PersistenceManager_onWLEDInstanceChanged(i)) {
          Serial.printf("[UI] Switched to WLED instance %d: %s (%s) - SAVED\n", 
                        i, friendlyName, ipAddress);
        } else {
          // Fallback if persistence fails
          CURRENT_WLED_INSTANCE = i;
          Serial.printf("[UI] Switched to WLED instance %d: %s (%s) - NOT SAVED\n", 
                        i, friendlyName, ipAddress);
        }
        
        // Force UI repaint to show new selection
        UIManager_needsFullRepaint = true;
        UIManager_paintPage();
      }
      break;
    }
  }
}
#endif // ENABLE_WLED_SELECTION_PAGE

void UIManager_init() {
  UIManager_needsFullRepaint = true;
  Serial.printf("[UI] Initialized with %d pages (brightness page %s)\n", 
                TOTAL_UI_PAGES, ENABLE_BRIGHTNESS_PAGE ? "ENABLED" : "DISABLED");
  
  // CRITICAL FIX: Don't paint page during init - SD card is unmounted and display might not be ready
  // UIManager_paintPage(); // This will be called later when system is fully ready
  
  Serial.println("[UI] Init complete - deferred painting until system ready");
}

// CRITICAL FIX: Post-initialization UI painting when system is ready
void UIManager_postInit() {
  Serial.println("[UI] Starting post-init painting...");
  
  // CRITICAL FIX: Force screen on and ensure backlight is working
  Serial.println("[UI] Forcing screen on...");
  DisplayManager_updateActivity();
  
  // Double-check backlight is actually on
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  Serial.printf("[UI] Backlight pin state: %s\n", digitalRead(PIN_LCD_BL) ? "HIGH" : "LOW");
  
  delay(100); // Give display time to stabilize
  
  Serial.printf("[UI] Display manager screen state: %s\n", DisplayManager_isScreenOn() ? "ON" : "OFF");
  
  // Now it's safe to paint the UI
  UIManager_paintPage();
  
  Serial.println("[UI] Post-init painting complete");
}

// OPTIMIZED: Smart UI updates with button press cleanup
void UIManager_update() {
  // Check if we should exit early boot mode
  if (UIManager_earlyBootMode && millis() > 15000) {
    UIManager_earlyBootMode = false;
    UIManager_needsFullRepaint = true;
    Serial.println("[UI] Exiting early boot mode - icons now available");
  }
  
  // Check if repaint is needed (e.g., after exiting early boot mode)
  if (UIManager_needsFullRepaint && DisplayManager_isScreenOn()) {
    UIManager_paintPage();
  }
  
  // Handle button press visual feedback timeout
  if (UIManager_pressedButtonIndex >= 0 && 
      millis() - UIManager_buttonPressTime > BUTTON_PRESS_FEEDBACK_MS) {
    
    // Restore button to normal state
    if (UIManager_pressedButtonIndex < 100) {
      // Page 0 button
      if (UIManager_pressedButtonIndex < UIManager_PAGE0_BTN_COUNT) {
        UIManager_drawIconButton(UIManager_page0Buttons[UIManager_pressedButtonIndex], false);
      }
    } else {
      // Page 1 button
      int index = UIManager_pressedButtonIndex - 100;
      if (index < UIManager_PAGE1_BTN_COUNT) {
        UIManager_drawIconButton(UIManager_page1Buttons[index], false);
      }
    }
    
    UIManager_pressedButtonIndex = -1;
  }
  
  // OPTIMIZED: Check if screen state changed and repaint if needed
  bool currentScreenState = DisplayManager_isScreenOn();
  if (currentScreenState != UIManager_lastScreenState) {
    UIManager_lastScreenState = currentScreenState;
    if (currentScreenState) {
      UIManager_needsFullRepaint = true;
      UIManager_paintPage();
    }
  }
  
  // Auto-refresh Now Playing page every 10 seconds - but only update what changed
  uint32_t now = millis();
  if (currentScreenState && UIManager_mapToPhysicalPage(UIManager_currentPage) == 2) { // Now Playing page
    if (now - UIManager_lastNowPlayingUpdate > NOW_PLAYING_UPDATE_INTERVAL_MS) {
      UIManager_lastNowPlayingUpdate = now;
      UIManager_checkAndUpdateNowPlaying();
    }
  }
}

int UIManager_getCurrentPage() {
  return UIManager_currentPage;
}

// Get the actual physical page being displayed
int UIManager_getCurrentPhysicalPage() {
  return UIManager_mapToPhysicalPage(UIManager_currentPage);
}

// OPTIMIZED: Force repaint function
void UIManager_forceRepaint() {
  UIManager_needsFullRepaint = true;
  UIManager_lastBrightnessValue = -1;
  UIManager_paintPage();
}

// OPTIMIZED: Get UI state
bool UIManager_needsRepaint() {
  return UIManager_needsFullRepaint;
}

// Selective update function for Now Playing page - only update what changed
void UIManager_checkAndUpdateNowPlaying() {
  if (!DisplayManager_isScreenOn()) {
    return;
  }
  
  // Fetch current WLED state
  JsonDocument doc;
  bool dataAvailable = WLEDClient_fetchWledState(doc);
  
  if (!dataAvailable) {
    Serial.println("[UI] Failed to fetch WLED state for selective update");
    return;
  }
  
  JsonObject state = doc["state"];
  if (!state || state.isNull()) {
    Serial.println("[UI] No state data for selective update");
    return;
  }
  
  // Get main segment
  int mainSeg = state["mainseg"] | 0;
  JsonArray segments = state["seg"];
  JsonObject seg;
  
  if (segments && !segments.isNull() && segments.size() > mainSeg) {
    seg = segments[mainSeg];
  } else if (segments && !segments.isNull() && segments.size() > 0) {
    seg = segments[0];
  }
  
  // Extract current IDs
  int presetId = state["ps"] | 0;
  int playlistId = state["pl"] | 0;
  int effectId = seg["fx"] | 0;
  int paletteId = seg["pal"] | 0;
  
  Serial.printf("[UI] Checking for changes: P=%d (was %d), PL=%d (was %d), E=%d (was %d), PAL=%d (was %d)\n",
                presetId, UIManager_lastPresetId, playlistId, UIManager_lastPlaylistId,
                effectId, UIManager_lastEffectId, paletteId, UIManager_lastPaletteId);
  
  // Check what changed
  bool presetChanged = (presetId != UIManager_lastPresetId) || (playlistId != UIManager_lastPlaylistId);
  bool effectChanged = (effectId != UIManager_lastEffectId);
  bool paletteChanged = (paletteId != UIManager_lastPaletteId);
  bool colorsChanged = paletteChanged; // Colors change when palette changes
  
  if (UIManager_forceNowPlayingUpdate || presetChanged || effectChanged || paletteChanged) {
    Serial.println("[UI] Changes detected - updating Now Playing display");
    
    // Update individual sections instead of full repaint
    UIManager_updateNowPlayingSections(state, seg, presetChanged, effectChanged, paletteChanged, colorsChanged);
    
    // Update tracked state
    UIManager_lastPresetId = presetId;
    UIManager_lastPlaylistId = playlistId;
    UIManager_lastEffectId = effectId;
    UIManager_lastPaletteId = paletteId;
    UIManager_forceNowPlayingUpdate = false;
  } else {
    Serial.println("[UI] No changes detected - skipping update");
  }
}

// Update specific sections of Now Playing display
void UIManager_updateNowPlayingSections(JsonObject state, JsonObject seg, 
                                       bool presetChanged, bool effectChanged, 
                                       bool paletteChanged, bool colorsChanged) {
  // For now, just do a full update - this could be optimized later to update individual text areas
  Serial.println("[UI] Performing selective update (full repaint for now)");
  UIManager_needsFullRepaint = true;
  UIManager_paintPage();
}

// NEW: Check if brightness page is enabled
bool UIManager_isBrightnessPageEnabled() {
  return ENABLE_BRIGHTNESS_PAGE;
}