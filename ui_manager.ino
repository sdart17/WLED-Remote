// ─────────────────────────────────────────────────────────────
// User Interface Management - OPTIMIZED with Brightness Page Control
// FIXED: Prevent touch events during swipe navigation
// ─────────────────────────────────────────────────────────────

struct IconBtn {
  int16_t x, y, w, h;
  const char* iconPath;
  const char* iconPressedPath;
  const char* fallbackText;
  uint8_t action;
};

struct BrightnessSlider {
  int16_t x, y, w, h;
  const char* bgPath;
  int16_t value; // 0..255
};

// UI manager state variables
static int UIManager_currentPage = 0;
static BrightnessSlider UIManager_brightnessSlider = { 50, 60, 140, 220, "/370x140-brightness-bw.bmp", 128 };
static int16_t UIManager_touchStartX = -1;
static int16_t UIManager_touchStartY = -1;
static bool UIManager_touchMoved = false;
static uint32_t UIManager_touchStartTime = 0;

// FIXED: Add swipe detection and suppression
static bool UIManager_swipeDetected = false;
static const uint32_t SWIPE_SUPPRESS_TIME_MS = 300; // Suppress touches for 300ms after swipe
static uint32_t UIManager_lastSwipeTime = 0;

// OPTIMIZED: Smart UI state tracking
static bool UIManager_needsFullRepaint = true;
static int UIManager_lastBrightnessValue = -1;
static bool UIManager_lastScreenState = true;

// OPTIMIZED: Button press tracking for visual feedback
static int UIManager_pressedButtonIndex = -1;
static uint32_t UIManager_buttonPressTime = 0;
static const uint32_t BUTTON_PRESS_FEEDBACK_MS = 150;

// Page 0 buttons
static IconBtn UIManager_page0Buttons[] = {
  { 30,  40, 80, 80, "/shuffle-80x80-wb-border.bmp", "/shuffle-80x80-bw-border.bmp", "SHFFL", 2},
  { 30, 130, 80, 80, "/bright-80x80-wb-border.bmp",  "/bright-80x80-bw-border.bmp",  "BRGHT", 1},
  { 30, 220, 80, 80, "/music-80x80-wb-border.bmp",   "/music-80x80-bw-border.bmp",   "MUSIC", 3},
  {130,  40, 80, 80, "/one-80x80-wb-border.bmp",     "/one-80x80-bw-border.bmp",     "ONE", 101},
  {130, 130, 80, 80, "/two-80x80-wb-border.bmp",     "/two-80x80-bw-border.bmp",     "TWO", 102},
  {130, 220, 80, 80, "/three-80x80-wb-border.bmp",   "/three-80x80-bw-border.bmp",   "THREE", 103}
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

// ENHANCED: Page mapping function to handle skipped brightness page
static int UIManager_mapToPhysicalPage(int logicalPage) {
#if ENABLE_BRIGHTNESS_PAGE
  return logicalPage; // Direct mapping when brightness page is enabled
#else
  // Skip brightness page (page 2) when disabled
  if (logicalPage >= 2) {
    return logicalPage + 1; // Map page 2 -> 3, etc.
  }
  return logicalPage; // Pages 0,1 remain unchanged
#endif
}

static int UIManager_mapFromPhysicalPage(int physicalPage) {
#if ENABLE_BRIGHTNESS_PAGE
  return physicalPage; // Direct mapping when brightness page is enabled
#else
  // Reverse mapping when brightness page is disabled
  if (physicalPage >= 3) {
    return physicalPage - 1; // Map physical page 3 -> logical page 2
  }
  return physicalPage; // Pages 0,1 remain unchanged
#endif
}

bool UIManager_hitTest(const IconBtn& b, int16_t x, int16_t y) {
  return (x >= b.x && x < (b.x + b.w) && y >= b.y && y < (b.y + b.h));
}

// OPTIMIZED: Enhanced text drawing with better performance
void UIManager_drawLabelCentered(int16_t x, int16_t y, int16_t w, int16_t h,
                                const char* s, uint16_t fg, uint16_t bg, uint8_t sz = 2) {
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
  static bool earlyBoot = true;
  if (earlyBoot && millis() > 15000) {
    earlyBoot = false;
    Serial.println("[UI] Exiting early boot mode - icons now available");
  }

  bool iconDrawn = false;
  
  if (!earlyBoot) {
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

// OPTIMIZED: Smart page painting with minimal redraws
void UIManager_paintPage() {
  Serial.printf("[UI] paintPage() start - screen on=%s\n", DisplayManager_isScreenOn() ? "Y" : "N");
  
  if (!DisplayManager_isScreenOn()) {
    Serial.println("[UI] paintPage() aborted - screen is off");
    return;
  }
  
  Serial.println("[UI] Getting TFT reference...");
  auto& tft = DisplayManager_getTFT();
  
  // CRITICAL FIX: Use direct TFT operations instead of DisplayManager functions during early boot
  if (UIManager_needsFullRepaint) {
    Serial.println("[UI] Clearing screen (direct TFT access)...");
    
    // Use direct TFT operations to avoid potential DisplayManager issues during boot
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[UI] Direct screen clear complete");
    
    // Skip battery drawing during early boot - it might be causing the hang
    Serial.println("[UI] Skipping battery status during early boot");
    
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
      
#if ENABLE_BRIGHTNESS_PAGE
    case 2: {
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
      for (int i = 0; i < UIManager_PAGE3_BTN_COUNT; i++) {
        UIManager_drawIconButton(UIManager_page3Buttons[i], false);
      }
      break;
  }
}

void UIManager_navToPage(int newPage) {
  int wrapped = (newPage % TOTAL_UI_PAGES + TOTAL_UI_PAGES) % TOTAL_UI_PAGES;
  if (wrapped != UIManager_currentPage) {
    UIManager_currentPage = wrapped;
    UIManager_needsFullRepaint = true; // Force full repaint on page change
    UIManager_lastBrightnessValue = -1; // Force slider redraw
    UIManager_paintPage();
    
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
      // OPTIMIZED: Immediate visual feedback - don't wait for network
      UIManager_drawIconButton(UIManager_page0Buttons[i], true); 
      UIManager_pressedButtonIndex = i;
      UIManager_buttonPressTime = millis();
      
      // PHASE 2B: Use network task for non-blocking execution
      uint8_t a = UIManager_page0Buttons[i].action;
      if (a >= 101 && a <= 103) {
        // Queue quick load command via network task
        NetworkTask_queueQuickLoad(a - 100);
      } else {
        // Queue preset command via network task
        NetworkTask_queuePreset(a);
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
#if ENABLE_BRIGHTNESS_PAGE
      } else if (physicalPage == 2) {
        UIManager_handlePage2Touch(x, y);
#endif
      } else if (physicalPage == 3) {
        UIManager_handlePage3Touch(x, y);
      }
    }
    return;
  }

#if ENABLE_BRIGHTNESS_PAGE
  // FIXED: Handle drag/hold for brightness slider (only if not suppressed and enabled)
  if (!UIManager_swipeDetected && UIManager_mapToPhysicalPage(UIManager_currentPage) == 2) {
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

// NEW: Check if brightness page is enabled
bool UIManager_isBrightnessPageEnabled() {
  return ENABLE_BRIGHTNESS_PAGE;
}