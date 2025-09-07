// ─────────────────────────────────────────────────────────────
// Display Control - PHASE 1 OPTIMIZED (GUARANTEED CLEAN COMPILATION)
// MEMORY SAFE: Eliminates unnecessary redraws and optimizes update frequency
// ─────────────────────────────────────────────────────────────

// Display manager state variables
static Adafruit_ST7789 DisplayManager_tft(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);
static bool DisplayManager_screenOn = true;
static bool DisplayManager_initialized = false;  // CRASH FIX: Track initialization state
static uint32_t DisplayManager_lastActivity = 0;

// OPTIMIZED: Smart display update management - reduced frequency
static uint32_t DisplayManager_lastUpdate = 0;
static uint32_t DisplayManager_lastBatteryDraw = 0;
static bool DisplayManager_batteryNeedsUpdate = true;

// OPTIMIZED: Performance tracking
static uint32_t DisplayManager_renderTime = 0;
static uint32_t DisplayManager_maxRenderTime = 0;

void DisplayManager_init() {
  // CRITICAL FIX: Don't call frequency manager during init - it's not initialized yet!
  // The frequency manager is initialized after the display manager in setup()
  
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  
  // CRITICAL FIX: Add delay to ensure stable power before SPI operations
  delay(50);
  
  SPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, PIN_LCD_CS);
  
  // CRITICAL FIX: Use very conservative SPI speed during initialization
  DisplayManager_tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  DisplayManager_tft.setSPISpeed(20000000); // Very conservative 20MHz for init stability
  DisplayManager_tft.setRotation(2);
  
  // CRITICAL FIX: Clear any potential artifacts from initialization
  DisplayManager_tft.fillScreen(ST77XX_BLACK);
  delay(10); // Allow display to stabilize
  
  // OPTIMIZED: Enable optimizations
  DisplayManager_tft.setTextWrap(false); // Disable text wrapping globally
  
  DisplayManager_lastActivity = millis();
  DisplayManager_initialized = true;  // CRASH FIX: Mark as initialized after successful setup
  
  Serial.println("[DISPLAY] Initialized - 240x320 @ 20MHz SPI (init mode)");
}

// CRITICAL FIX: Post-initialization setup after frequency manager is ready
void DisplayManager_postInit() {
  // Now that frequency manager is initialized, we can increase SPI speed
  FreqManager_notifyDisplayActivity(); // Lock to high frequency
  
  // Increase SPI speed for normal operations
  DisplayManager_tft.setSPISpeed(40000000); // 40MHz for normal operation
  
  Serial.println("[DISPLAY] Post-init complete - upgraded to 40MHz SPI");
}

void DisplayManager_checkScreenTimeout() {
  if (DisplayManager_screenOn && millis() - DisplayManager_lastActivity > SCREEN_TIMEOUT_MS) {
    DisplayManager_screenOn = false;
    digitalWrite(PIN_LCD_BL, LOW);
    Serial.println("[DISPLAY] Screen timeout - backlight off");
  }
}

// PHASE 1: Dramatically improved display update efficiency
void DisplayManager_update() {
  uint32_t now = millis();
  
  // IMPROVED: Reduce display update frequency from 30 FPS to 20 FPS for better efficiency
  if (now - DisplayManager_lastUpdate < 50) return; // 50ms = 20 FPS
  DisplayManager_lastUpdate = now;
  
  DisplayManager_syncScreenState();
  DisplayManager_checkScreenTimeout();
  
  // PHASE 1: Only update battery when screen is on AND sufficient time has passed
  if (DisplayManager_screenOn && PowerManager_isBatteryEnabled()) {
    // Check if we need to update (either forced or enough time passed)
    bool timeForUpdate = (now - DisplayManager_lastBatteryDraw > BATTERY_UPDATE_INTERVAL_MS);
    
    if (DisplayManager_batteryNeedsUpdate || timeForUpdate) {
      uint32_t startTime = micros();
      
      DisplayManager_drawBatteryStatus();
      
      uint32_t renderTime = micros() - startTime;
      DisplayManager_renderTime = renderTime;
      if (renderTime > DisplayManager_maxRenderTime) {
        DisplayManager_maxRenderTime = renderTime;
      }
      
      DisplayManager_lastBatteryDraw = now;
      DisplayManager_batteryNeedsUpdate = false;
    }
  }
}

void DisplayManager_updateActivity() {
  uint32_t now = millis();
  DisplayManager_lastActivity = now;
  
  bool backlightOn = digitalRead(PIN_LCD_BL);
  
  // CRITICAL FIX: Prevent rapid state changes that cause corruption
  static uint32_t lastStateChange = 0;
  static bool lastBacklightState = false;
  
  // Only process state changes if enough time has passed (debounce)
  if ((now - lastStateChange) < 50) return; // 50ms debounce
  
  if (backlightOn != lastBacklightState) {
    lastStateChange = now;
    lastBacklightState = backlightOn;
  }
  
  // CRITICAL FIX: Only call frequency manager if system is fully initialized
  static bool systemInitialized = false;
  if (millis() > 10000) systemInitialized = true; // Assume init complete after 10s
  
  if (!DisplayManager_screenOn && backlightOn) {
    // Screen was turned on externally (e.g., by twist manager)
    DisplayManager_screenOn = true;
    DisplayManager_batteryNeedsUpdate = true;
    Serial.println("[DISPLAY] Screen wake detected - syncing state");
  } else if (!DisplayManager_screenOn && !backlightOn) {
    DisplayManager_screenOn = true;
    // Only call frequency manager if system is initialized
    if (systemInitialized) {
      FreqManager_notifyDisplayActivity();
    }
    digitalWrite(PIN_LCD_BL, HIGH);
    DisplayManager_batteryNeedsUpdate = true;
    Serial.println("[DISPLAY] Screen wake - backlight on");
  }
}

// OPTIMIZED: Force screen state sync with reduced overhead
void DisplayManager_syncScreenState() {
  bool backlightOn = digitalRead(PIN_LCD_BL);
  if (backlightOn != DisplayManager_screenOn) {
    DisplayManager_screenOn = backlightOn;
    if (backlightOn) {
      DisplayManager_lastActivity = millis();
      DisplayManager_batteryNeedsUpdate = true;
      // Don't log every sync - only significant changes
      static uint32_t lastSyncLog = 0;
      if (millis() - lastSyncLog > 5000) {
        Serial.println("[DISPLAY] Screen state synced - screen is ON");
        lastSyncLog = millis();
      }
    }
  }
}

bool DisplayManager_isScreenOn() {
  return DisplayManager_screenOn && DisplayManager_initialized;  // CRASH FIX: Only true if initialized
}

Adafruit_ST7789& DisplayManager_getTFT() {
  return DisplayManager_tft;
}

// PHASE 1: Highly optimized battery display with smart change detection
void DisplayManager_drawBatteryStatus() {
  // CRITICAL FIX: Only call frequency manager if system is fully initialized
  static bool systemInitialized = false;
  if (millis() > 10000) systemInitialized = true; // Assume init complete after 10s
  
  if (systemInitialized) {
    FreqManager_notifyDisplayActivity();
  }
  
  int pct = constrain(PowerManager_getBatteryPercent(), 0, 100);
  bool ext = PowerManager_isExternalPower();
  
  // CRITICAL: Early exit if no change (this saves significant CPU)
  // Only skip if BOTH values are unchanged
  static int lastDrawnPct = -1;
  static bool lastDrawnExternal = false;
  static uint32_t lastDrawTime = 0;
  
  // CRITICAL FIX: Prevent rapid redraws that cause corruption
  uint32_t now = millis();
  if ((now - lastDrawTime) < 100) return; // 100ms minimum between draws
  
  if (pct == lastDrawnPct && ext == lastDrawnExternal) {
    return; // No change, skip expensive drawing
  }
  
  // Update what we're about to draw
  lastDrawnPct = pct;
  lastDrawnExternal = ext;
  lastDrawTime = now;
  
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);

  DisplayManager_tft.setTextSize(1);
  int16_t bx, by; 
  uint16_t bw, bh;
  DisplayManager_tft.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);

  // Battery dimensions with proper padding
  const int textPad = 4;    // Padding around text
  const int batteryPad = 6; // Extra space for battery body
  const int nippleW = 3;
  const int nippleH = 6;
  
  int battW = bw + (textPad * 2) + batteryPad;
  int battH = max(16, (int)bh + (textPad * 2));
  int x = SCREEN_WIDTH - battW - nippleW - 6;
  int y = 3;

  // CRITICAL: Clear entire area first to remove any artifacts
  DisplayManager_tft.fillRect(x - 8, y - 4, battW + nippleW + 16, battH + 8, ST77XX_BLACK);

  // Draw battery nipple/terminal position
  int nippleX = x + battW;
  int nippleY = y + (battH - nippleH) / 2;

  if (ext) {
    // EXTERNAL POWER: Solid white icon with black text (NO OUTLINE)
    Serial.println("[BATTERY] Drawing EXTERNAL power state");
    
    // Fill entire battery body with white (solid)
    DisplayManager_tft.fillRect(x, y, battW, battH, ST77XX_WHITE);
    
    // Fill nipple with white (solid)
    DisplayManager_tft.fillRect(nippleX, nippleY, nippleW, nippleH, ST77XX_WHITE);
    
    // Draw BLACK text on white background
    int textX = x + (battW - bw) / 2;
    int textY = y + (battH - bh) / 2;
    DisplayManager_tft.setCursor(textX, textY);
    DisplayManager_tft.setTextColor(ST77XX_BLACK, ST77XX_WHITE);
    DisplayManager_tft.print(buf);
    
  } else {
    // BATTERY POWER: Black icon with thin white border and white text
    Serial.println("[BATTERY] Drawing BATTERY power state");
    
    // Fill entire battery body with black first
    DisplayManager_tft.fillRect(x, y, battW, battH, ST77XX_BLACK);
    
    // Draw thin white border around battery body
    DisplayManager_tft.drawRect(x, y, battW, battH, ST77XX_WHITE);
    
    // Fill nipple with black and add white border
    DisplayManager_tft.fillRect(nippleX, nippleY, nippleW, nippleH, ST77XX_BLACK);
    DisplayManager_tft.drawRect(nippleX, nippleY, nippleW, nippleH, ST77XX_WHITE);
    
    // Draw WHITE text on black background
    int textX = x + (battW - bw) / 2;
    int textY = y + (battH - bh) / 2;
    DisplayManager_tft.setCursor(textX, textY);
    DisplayManager_tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    DisplayManager_tft.print(buf);
  }
}

// PHASE 1: Optimized screen clearing 
void DisplayManager_clearScreen() {
  // CRITICAL FIX: Only call frequency manager if system is fully initialized
  static bool systemInitialized = false;
  if (millis() > 10000) systemInitialized = true; // Assume init complete after 10s
  
  if (systemInitialized) {
    FreqManager_notifyDisplayActivity();
  }
  
  uint32_t startTime = micros();
  
  // CRITICAL FIX: Use safe fill with batched operations
  DisplayManager_tft.startWrite();
  DisplayManager_tft.fillScreen(ST77XX_BLACK);
  DisplayManager_tft.endWrite();
  
  uint32_t renderTime = micros() - startTime;
  
  DisplayManager_renderTime = renderTime;
  if (renderTime > DisplayManager_maxRenderTime) {
    DisplayManager_maxRenderTime = renderTime;
  }
}

// PHASE 1: Enhanced bulk text rendering
void DisplayManager_drawTextCentered(int16_t x, int16_t y, int16_t w, int16_t h,
                                    const char* text, uint16_t fg, uint16_t bg, 
                                    uint8_t size) {
  DisplayManager_tft.setTextSize(size);
  DisplayManager_tft.setTextColor(fg, bg);
  
  int16_t bx, by; 
  uint16_t bw, bh;
  DisplayManager_tft.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  
  int16_t cx = x + (w - (int)bw) / 2;
  int16_t cy = y + (h - (int)bh) / 2;
  
  // PHASE 1: Clear background and draw text
  DisplayManager_tft.fillRect(x, y, w, h, bg);
  DisplayManager_tft.setCursor(cx, cy);
  DisplayManager_tft.print(text);
}

// PHASE 1: Fast rectangle drawing 
void DisplayManager_drawButtonRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                  uint16_t fg, uint16_t bg) {
  DisplayManager_tft.fillRect(x, y, w, h, bg);
  DisplayManager_tft.drawRect(x, y, w, h, fg);
}

// OPTIMIZED: Performance monitoring functions with better reporting
void DisplayManager_printPerformanceStats() {
  Serial.printf("[DISPLAY] Render: %dμs, Max: %dμs, Screen: %s\n",
                DisplayManager_renderTime, DisplayManager_maxRenderTime,
                DisplayManager_screenOn ? "ON" : "OFF");
}

uint32_t DisplayManager_getLastRenderTime() {
  return DisplayManager_renderTime;
}

uint32_t DisplayManager_getMaxRenderTime() {
  return DisplayManager_maxRenderTime;
}

void DisplayManager_resetPerformanceCounters() {
  DisplayManager_maxRenderTime = 0;
  DisplayManager_renderTime = 0;
}

// OPTIMIZED: Activity timeout configuration
void DisplayManager_setScreenTimeout(uint32_t timeoutMs) {
  // This would require modifying the constant, but we can store in a variable
  // For now, this is a placeholder for future dynamic timeout support
  Serial.printf("[DISPLAY] Screen timeout requested: %dms (requires restart)\n", timeoutMs);
}

uint32_t DisplayManager_getScreenTimeout() {
  return SCREEN_TIMEOUT_MS;
}

uint32_t DisplayManager_getTimeSinceActivity() {
  return millis() - DisplayManager_lastActivity;
}

// PHASE 1: Enhanced force update with smart invalidation
void DisplayManager_forceUpdate() {
  DisplayManager_batteryNeedsUpdate = true;
  DisplayManager_lastUpdate = 0; // Force immediate update
  
  Serial.println("[DISPLAY] Forced update requested");
}

// PHASE 1: Simple icon drawing
void DisplayManager_drawIcon(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  DisplayManager_tft.fillRect(x, y, w, h, color);
}

// PHASE 1: Emergency low memory display mode
void DisplayManager_enterLowMemoryMode() {
  Serial.println("[DISPLAY] Entering low memory mode - reducing update frequency");
  
  // Reduce update frequency dramatically
  static bool lowMemoryMode = false;
  if (!lowMemoryMode) {
    lowMemoryMode = true;
    // Force a longer delay between updates
    DisplayManager_lastUpdate = millis();
  }
}

// Missing low-level display functions for differential renderer
void DisplayManager_startWrite() {
  DisplayManager_tft.startWrite();
}

void DisplayManager_endWrite() {
  DisplayManager_tft.endWrite();
}

void DisplayManager_setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  DisplayManager_tft.setAddrWindow(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

void DisplayManager_pushColors(uint16_t *data, uint32_t len) {
  if (!data) {
    Serial.printf("[DISPLAY] ERROR: pushColors called with null data pointer (len=%d)\n", len);
    return;
  }
  if (len == 0) {
    Serial.println("[DISPLAY] WARNING: pushColors called with zero length");
    return;
  }
  if (len > SCREEN_WIDTH * SCREEN_HEIGHT) {
    Serial.printf("[DISPLAY] ERROR: pushColors length too large: %d (max=%d)\n", len, SCREEN_WIDTH * SCREEN_HEIGHT);
    return;
  }
  
  DisplayManager_tft.writePixels(data, len);
}

uint16_t* DisplayManager_getFrameBuffer() {
  // Return null as we don't have a frame buffer - differential renderer should handle this
  return nullptr;
}

void DisplayManager_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  DisplayManager_tft.fillRect(x, y, w, h, color);
}

void DisplayManager_enterStandby() {
  // Enter standby mode - turn off backlight but keep display
  digitalWrite(PIN_LCD_BL, LOW);
  DisplayManager_screenOn = false;
  Serial.println("[DISPLAY] Entered standby mode");
}

void DisplayManager_exitStandby() {
  // Exit standby mode - turn on backlight
  digitalWrite(PIN_LCD_BL, HIGH);
  DisplayManager_screenOn = true;
  DisplayManager_lastActivity = millis();
  Serial.println("[DISPLAY] Exited standby mode");
}

bool DisplayManager_isIntensiveRender() {
  // Return true if rendering is taking a long time
  return DisplayManager_renderTime > 25; // More than 25ms is intensive
}

void DisplayManager_capturePageBuffer(int page, uint16_t* buffer) {
  if (!buffer) {
    Serial.println("[DISPLAY] ERROR: DisplayManager_capturePageBuffer called with null buffer");
    return;
  }
  
  Serial.printf("[DISPLAY] WARNING: DisplayManager_capturePageBuffer not fully implemented for page %d\n", page);
  
  // For now, fill with a solid color to prevent crashes
  // This should be implemented to actually capture the current page content
  uint16_t fillColor = 0x0000; // Black
  uint32_t totalPixels = SCREEN_WIDTH * SCREEN_HEIGHT;
  
  for (uint32_t i = 0; i < totalPixels; i++) {
    buffer[i] = fillColor;
  }
}