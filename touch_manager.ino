// ─────────────────────────────────────────────────────────────
// Touch Input Handling - PHASE 1 OPTIMIZED (COMPILATION FIXED)
// IMPROVED: Reduced reinit frequency and better error recovery
// ─────────────────────────────────────────────────────────────

// Touch manager state variables
static CSE_CST328 TouchManager_ctp(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, PIN_TP_RST, PIN_TP_INT);
static bool TouchManager_touching = false;
static uint32_t TouchManager_lastTouchSeenMs = 0;
static uint32_t TouchManager_lastTPReinitMs = 0;

// OPTIMIZED: High-frequency touch polling with separate update intervals
static uint32_t TouchManager_lastFastUpdate = 0;
static uint32_t TouchManager_lastSlowUpdate = 0;

// PHASE 1: Less aggressive error thresholds for better stability
static const uint32_t TOUCH_DEBOUNCE_MS = 15; // IMPROVED: Slightly increased from 10ms
static uint32_t TouchManager_lastValidTouch = 0;
static int16_t TouchManager_lastX = -1, TouchManager_lastY = -1;
static const uint8_t TOUCH_COORD_FILTER_THRESHOLD = 8; // IMPROVED: Increased from 5

// PHASE 1: More lenient error handling for stability  
static uint8_t TouchManager_badReadings = 0;
static const uint8_t MAX_BAD_READINGS = 8; // IMPROVED: Increased from 5 for stability

// PHASE 1: Less aggressive I2C error recovery
static uint8_t TouchManager_i2cErrors = 0;
static const uint8_t MAX_I2C_ERRORS = 5; // IMPROVED: Increased from 3

// PHASE 3: Advanced gesture recognition state
#if ENABLE_ADVANCED_GESTURES
static uint32_t TouchManager_lastTapTime = 0;
static uint8_t TouchManager_tapCount = 0;
static int16_t TouchManager_pressStartX = -1, TouchManager_pressStartY = -1;
static uint32_t TouchManager_pressStartTime = 0;
static bool TouchManager_longPressDetected = false;
static bool TouchManager_doubleTapDetected = false;

#if ENABLE_MULTI_TOUCH
static bool TouchManager_multiTouchActive = false;
static int16_t TouchManager_touch2X = -1, TouchManager_touch2Y = -1;
static uint16_t TouchManager_initialDistance = 0;
#endif

#if ENABLE_TOUCH_PRESSURE
static uint8_t TouchManager_pressureLevel = 0;
static uint32_t TouchManager_lastPressureUpdate = 0;
#endif
#endif // ENABLE_ADVANCED_GESTURES

// PHASE 1: Use config constants instead of redefining them here
// TOUCH_IDLE_TIMEOUT_MS and TOUCH_REINIT_INTERVAL_MS are defined in config.ino

void TouchManager_reinitTouch() {
  TouchManager_lastTPReinitMs = millis();

  Serial.println("[TOUCH] Reinitializing controller (less aggressive mode)...");
  
  // PHASE 2B: High-speed I2C reset sequence
  Wire.end();
  delay(20); // IMPROVED: Longer delay for stability
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL);
  Wire.setClock(1000000);  // PHASE 2B: Increase to 1MHz for lower latency
  
  // PHASE 1: Enhanced hardware reset sequence
  digitalWrite(PIN_TP_RST, LOW);  
  delay(15);  // IMPROVED: Longer reset pulse
  digitalWrite(PIN_TP_RST, HIGH); 
  delay(100); // IMPROVED: Much longer recovery time

  // Re-initialize the CSE_CST328 controller
  if (!TouchManager_ctp.begin()) {
    Serial.println("[TOUCH] Reinit FAILED - will retry later");
    TouchManager_i2cErrors++;
  } else {
    Serial.println("[TOUCH] Reinit successful - errors cleared");
    TouchManager_i2cErrors = 0;
  }

  // Clear state
  TouchManager_touching = false;
  TouchManager_lastValidTouch = 0;
  TouchManager_lastTouchSeenMs = millis();
  TouchManager_badReadings = 0;
  TouchManager_lastX = -1;
  TouchManager_lastY = -1;
}

void TouchManager_init() {
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 1000000); // PHASE 2B: 1MHz for lower latency
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  pinMode(PIN_TP_RST, OUTPUT);
  
  // PHASE 1: Enhanced initialization sequence
  digitalWrite(PIN_TP_RST, LOW);  
  delay(20);  // Longer initial reset
  digitalWrite(PIN_TP_RST, HIGH); 
  delay(150); // Much longer initial recovery
  
  if (!TouchManager_ctp.begin()) {
    Serial.println("[TP] begin() FAILED - will auto-recover");
    TouchManager_i2cErrors = MAX_I2C_ERRORS; // Force reinit on first update
  } else {
    Serial.printf("[TP] CST328 ready (RST=%d INT=%d) - 1MHz mode\n", PIN_TP_RST, PIN_TP_INT);
    TouchManager_i2cErrors = 0;
  }
}

// PHASE 1: Optimized touch polling with improved error handling
void TouchManager_fastUpdate() {
  uint32_t now = millis();
  if (now - TouchManager_lastFastUpdate < TOUCH_UPDATE_INTERVAL_MS) return;
  TouchManager_lastFastUpdate = now;
  
  // PHASE 1: More lenient reinit check - only if really necessary
  if (TouchManager_i2cErrors >= MAX_I2C_ERRORS) {
    if (now - TouchManager_lastTPReinitMs > TOUCH_REINIT_INTERVAL_MS) {
      TouchManager_reinitTouch();
    }
    return;
  }

  // Add comprehensive safety checks for touch controller calls
  bool nowTouched = false;
  
  // Check if controller is in valid state before calling isTouched()
  if (TouchManager_i2cErrors >= MAX_I2C_ERRORS) {
    return; // Controller is in error state
  }
  
  nowTouched = TouchManager_ctp.isTouched();
  
  // PHASE 1: Enhanced debouncing with longer minimum interval
  if (nowTouched && (now - TouchManager_lastValidTouch < TOUCH_DEBOUNCE_MS)) {
    return; // Too soon after last touch
  }
  
  if (nowTouched) {
    int16_t x = 0, y = 0;
    
    // Add safety check and bounds validation before processing touch point
    auto p = TouchManager_ctp.getPoint(0);
    x = p.x;
    y = p.y;
    
    // Additional null/invalid data check for extreme values that suggest hardware issues
    if (x == -1 || y == -1 || (x == 0 && y == 0)) {
      TouchManager_badReadings++;
      if (TouchManager_badReadings > 3) {
        Serial.printf("[TOUCH] Suspicious coordinates: x=%d, y=%d (bad_readings=%d)\n", x, y, TouchManager_badReadings);
      }
      return;
    }
    
    // PHASE 1: More lenient coordinate validation
    if (x < -10 || x >= SCREEN_WIDTH + 10 || y < -10 || y >= SCREEN_HEIGHT + 10) {
      TouchManager_badReadings++;
      
      // PHASE 1: Only trigger reinit after more bad readings
      if (TouchManager_badReadings >= MAX_BAD_READINGS) {
        Serial.printf("[TOUCH] Too many bad readings (%d) - will reinit later\n", TouchManager_badReadings);
        TouchManager_i2cErrors++; // Increment instead of immediately forcing reinit
        TouchManager_badReadings = 0; // Reset counter
      }
      return;
    }
    
    // Clamp coordinates to valid screen bounds
    x = constrain(x, 0, SCREEN_WIDTH - 1);
    y = constrain(y, 0, SCREEN_HEIGHT - 1);
    
    // PHASE 1: Enhanced coordinate filtering with adaptive threshold
    if (TouchManager_lastX >= 0 && TouchManager_lastY >= 0) {
      int16_t dx = abs(x - TouchManager_lastX);
      int16_t dy = abs(y - TouchManager_lastY);
      
      // Use larger threshold if we've had recent bad readings
      uint8_t adaptiveThreshold = TOUCH_COORD_FILTER_THRESHOLD;
      if (TouchManager_badReadings > 2) {
        adaptiveThreshold += 3; // Be more forgiving during instability
      }
      
      if (dx < adaptiveThreshold && dy < adaptiveThreshold) {
        return; // Ignore micro-movements
      }
    }
    
    TouchManager_lastValidTouch = now;
    TouchManager_lastX = x;
    TouchManager_lastY = y;
    TouchManager_badReadings = 0; // Reset on good reading
    
    if (!TouchManager_touching) {
      if (!DisplayManager_isScreenOn()) { 
        DisplayManager_updateActivity(); 
        TouchManager_touching = true; 
        TouchManager_lastTouchSeenMs = now; 
      } else {           
        #if ENABLE_ADVANCED_GESTURES
        TouchManager_handlePressStart(x, y, now);
        #endif
        UIManager_handleTouch(x, y, true, false); 
        TouchManager_touching = true; 
        TouchManager_lastTouchSeenMs = now; 
      }
    } else {
      #if ENABLE_ADVANCED_GESTURES
      TouchManager_handleDrag(x, y, now);
      #endif
      UIManager_handleTouch(x, y, false, false);
      TouchManager_lastTouchSeenMs = now;
    }
  } else if (TouchManager_touching) {
    // Use last known good coordinates for release event
    #if ENABLE_ADVANCED_GESTURES
    TouchManager_handleRelease(TouchManager_lastX, TouchManager_lastY, millis());
    #endif
    UIManager_handleTouch(TouchManager_lastX, TouchManager_lastY, false, true);
    TouchManager_touching = false;
    TouchManager_lastX = -1;
    TouchManager_lastY = -1;
  }
}

// PHASE 1: Less aggressive maintenance tasks
void TouchManager_slowUpdate() {
  uint32_t now = millis();
  if (now - TouchManager_lastSlowUpdate < 2000) return; // IMPROVED: 2s interval instead of 1s
  TouchManager_lastSlowUpdate = now;

  // PHASE 1: Use config constant for idle timeout (increased in config from 8s to 12s would be ideal)
  if (now - TouchManager_lastTouchSeenMs > TOUCH_IDLE_TIMEOUT_MS * 1.5) { // 1.5x config value for extra stability
    if (now - TouchManager_lastTPReinitMs > TOUCH_REINIT_INTERVAL_MS) {
      Serial.println("[TOUCH] Extended idle timeout - scheduling gentle reinit");
      TouchManager_i2cErrors = MAX_I2C_ERRORS - 1; // Don't force immediate reinit
    }
  }
  
  // PHASE 1: More informative I2C health monitoring
  if (TouchManager_i2cErrors >= MAX_I2C_ERRORS - 1) {
    Serial.printf("[TOUCH] I2C health: %d/%d errors, bad reads: %d\n", 
                  TouchManager_i2cErrors, MAX_I2C_ERRORS, TouchManager_badReadings);
  }
}

// PHASE 1: Enhanced update function with better yielding
void TouchManager_update() {
  TouchManager_fastUpdate();  // 1ms polling for responsiveness
  TouchManager_slowUpdate();  // 2s maintenance tasks
  
  // PHASE 1: Less frequent yielding to reduce overhead
  static uint8_t yieldCounter = 0;
  if (++yieldCounter >= 20) { // IMPROVED: Yield every 20th fast update instead of 10th
    yield();
    yieldCounter = 0;
  }
}

// PHASE 1: Enhanced health check functions
bool TouchManager_isHealthy() {
  return TouchManager_i2cErrors < (MAX_I2C_ERRORS - 1) && TouchManager_badReadings < (MAX_BAD_READINGS / 2);
}

uint8_t TouchManager_getBadReadingCount() {
  return TouchManager_badReadings;
}

uint32_t TouchManager_getLastTouchTime() {
  return TouchManager_lastTouchSeenMs;
}

uint8_t TouchManager_getErrorCount() {
  return TouchManager_i2cErrors;
}

// PHASE 1: More gentle force reinit function
void TouchManager_forceReinit() {
  Serial.println("[TOUCH] Gentle reinit requested");
  TouchManager_i2cErrors = MAX_I2C_ERRORS - 1; // Don't force immediate reinit
}

// PHASE 1: Recovery function for emergency situations only
void TouchManager_emergencyRecovery() {
  Serial.println("[TOUCH] Emergency recovery initiated");
  
  // More aggressive recovery only in emergency
  TouchManager_i2cErrors = MAX_I2C_ERRORS;
  TouchManager_badReadings = MAX_BAD_READINGS;
  TouchManager_lastTPReinitMs = 0; // Force immediate reinit
  
  TouchManager_reinitTouch();
}

// PHASE 1: Get touch controller statistics
void TouchManager_printStats() {
  uint32_t uptime = millis();
  uint32_t timeSinceTouch = uptime - TouchManager_lastTouchSeenMs;
  uint32_t timeSinceReinit = uptime - TouchManager_lastTPReinitMs;
  
  Serial.printf("[TOUCH] Stats: Errors=%d/%d, BadReads=%d/%d, LastTouch=%ds ago, LastReinit=%ds ago\n",
                TouchManager_i2cErrors, MAX_I2C_ERRORS,
                TouchManager_badReadings, MAX_BAD_READINGS,
                timeSinceTouch / 1000,
                timeSinceReinit / 1000);
}

// PHASE 1: Check if touch system needs attention
bool TouchManager_needsMaintenance() {
  uint32_t now = millis();
  
  // Need maintenance if too many errors or too long since last touch
  return (TouchManager_i2cErrors >= MAX_I2C_ERRORS / 2) ||
         (TouchManager_badReadings >= MAX_BAD_READINGS / 2) ||
         (now - TouchManager_lastTouchSeenMs > TOUCH_IDLE_TIMEOUT_MS * 2);
}

// PHASE 1: Get touch system health score (0-100)
uint8_t TouchManager_getHealthScore() {
  uint8_t score = 100;
  
  // Deduct points for errors
  score -= (TouchManager_i2cErrors * 20);
  score -= (TouchManager_badReadings * 10);
  
  // Deduct points for old reinits
  uint32_t timeSinceReinit = millis() - TouchManager_lastTPReinitMs;
  if (timeSinceReinit < TOUCH_REINIT_INTERVAL_MS / 2) {
    score -= 20; // Recent reinit indicates problems
  }
  
  return max(0, (int)score);
}

// ═══════════════════════════════════════════════════════════════
// PHASE 3: Advanced Touch Gesture Recognition Functions
// ═══════════════════════════════════════════════════════════════

#if ENABLE_ADVANCED_GESTURES

// Calculate distance between two points
static uint16_t TouchManager_calculateDistance(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
  int32_t dx = x1 - x2;
  int32_t dy = y1 - y2;
  return (uint16_t)sqrt(dx * dx + dy * dy);
}

// Handle press start - initialize gesture tracking
void TouchManager_handlePressStart(int16_t x, int16_t y, uint32_t timestamp) {
  TouchManager_pressStartX = x;
  TouchManager_pressStartY = y;
  TouchManager_pressStartTime = timestamp;
  TouchManager_longPressDetected = false;
  TouchManager_doubleTapDetected = false;
  
  #if ENABLE_TOUCH_PRESSURE
  TouchManager_pressureLevel = 1; // Start with light pressure
  TouchManager_lastPressureUpdate = timestamp;
  #endif
  
  // Check for double tap
  if (timestamp - TouchManager_lastTapTime < GESTURE_DOUBLE_TAP_TIME_MS) {
    TouchManager_tapCount++;
    if (TouchManager_tapCount >= 2) {
      TouchManager_doubleTapDetected = true;
      TouchManager_tapCount = 0;
      Serial.printf("[GESTURE] Double tap detected at (%d,%d)\n", x, y);
      // Could trigger special actions here
    }
  } else {
    TouchManager_tapCount = 1;
  }
  
  TouchManager_lastTapTime = timestamp;
  
  #if ENABLE_MULTI_TOUCH
  // Reset multi-touch state
  TouchManager_multiTouchActive = false;
  TouchManager_touch2X = -1;
  TouchManager_touch2Y = -1;
  TouchManager_initialDistance = 0;
  #endif
  
  Serial.printf("[GESTURE] Press start: (%d,%d) tap_count=%d\n", x, y, TouchManager_tapCount);
}

// Handle drag movement - update gesture state
void TouchManager_handleDrag(int16_t x, int16_t y, uint32_t timestamp) {
  if (TouchManager_pressStartX < 0 || TouchManager_pressStartY < 0) return;
  
  uint32_t pressTime = timestamp - TouchManager_pressStartTime;
  uint16_t dragDistance = TouchManager_calculateDistance(x, y, TouchManager_pressStartX, TouchManager_pressStartY);
  
  // Long press detection
  if (!TouchManager_longPressDetected && pressTime > GESTURE_LONG_PRESS_TIME_MS && dragDistance < 10) {
    TouchManager_longPressDetected = true;
    Serial.printf("[GESTURE] Long press detected at (%d,%d) after %dms\n", x, y, pressTime);
    // Could trigger context menu or special actions
  }
  
  #if ENABLE_TOUCH_PRESSURE
  // Simulate pressure based on movement speed and duration
  if (timestamp - TouchManager_lastPressureUpdate > 100) { // Update every 100ms
    if (dragDistance > 5) {
      TouchManager_pressureLevel = min(TOUCH_PRESSURE_THRESHOLD, TouchManager_pressureLevel + 1);
    } else if (pressTime > 500) {
      TouchManager_pressureLevel = min(TOUCH_PRESSURE_THRESHOLD, TouchManager_pressureLevel + 1);
    }
    TouchManager_lastPressureUpdate = timestamp;
    
    // Log pressure changes
    static uint8_t lastLoggedPressure = 0;
    if (TouchManager_pressureLevel != lastLoggedPressure) {
      Serial.printf("[GESTURE] Pressure level: %d/%d\n", TouchManager_pressureLevel, TOUCH_PRESSURE_THRESHOLD);
      lastLoggedPressure = TouchManager_pressureLevel;
    }
  }
  #endif
  
  #if ENABLE_MULTI_TOUCH
  // Multi-touch detection (simplified - would need actual multi-touch hardware support)
  // This is a placeholder for future enhancement
  if (!TouchManager_multiTouchActive && dragDistance > GESTURE_PINCH_THRESHOLD * 2) {
    // Could detect second touch point here with proper hardware
    Serial.printf("[GESTURE] Potential multi-touch pattern detected\n");
  }
  #endif
}

// Handle touch release - finalize gesture recognition
void TouchManager_handleRelease(int16_t x, int16_t y, uint32_t timestamp) {
  if (TouchManager_pressStartX < 0 || TouchManager_pressStartY < 0) return;
  
  uint32_t totalPressTime = timestamp - TouchManager_pressStartTime;
  uint16_t totalDragDistance = TouchManager_calculateDistance(x, y, TouchManager_pressStartX, TouchManager_pressStartY);
  
  // Classify the gesture
  if (TouchManager_doubleTapDetected) {
    Serial.printf("[GESTURE] Completed: Double tap (total_time=%dms)\n", totalPressTime);
  } else if (TouchManager_longPressDetected) {
    Serial.printf("[GESTURE] Completed: Long press (duration=%dms, drag=%dpx)\n", totalPressTime, totalDragDistance);
  } else if (totalDragDistance > SWIPE_THRESHOLD) {
    Serial.printf("[GESTURE] Completed: Swipe/Drag (distance=%dpx, time=%dms)\n", totalDragDistance, totalPressTime);
  } else if (totalPressTime < 200) {
    Serial.printf("[GESTURE] Completed: Quick tap (time=%dms)\n", totalPressTime);
  } else {
    Serial.printf("[GESTURE] Completed: Standard tap (time=%dms)\n", totalPressTime);
  }
  
  #if ENABLE_TOUCH_PRESSURE
  if (TouchManager_pressureLevel > 1) {
    Serial.printf("[GESTURE] Final pressure level: %d/%d\n", TouchManager_pressureLevel, TOUCH_PRESSURE_THRESHOLD);
  }
  #endif
  
  // Reset gesture state
  TouchManager_pressStartX = -1;
  TouchManager_pressStartY = -1;
  TouchManager_pressStartTime = 0;
  TouchManager_longPressDetected = false;
  // Keep TouchManager_doubleTapDetected for potential chaining
  
  #if ENABLE_TOUCH_PRESSURE
  TouchManager_pressureLevel = 0;
  #endif
  
  #if ENABLE_MULTI_TOUCH
  TouchManager_multiTouchActive = false;
  TouchManager_touch2X = -1;
  TouchManager_touch2Y = -1;
  TouchManager_initialDistance = 0;
  #endif
}

// Get current gesture state for external use
bool TouchManager_isLongPressActive() {
  return TouchManager_longPressDetected;
}

bool TouchManager_isDoubleTapDetected() {
  return TouchManager_doubleTapDetected;
}

uint8_t TouchManager_getCurrentPressureLevel() {
  #if ENABLE_TOUCH_PRESSURE
  return TouchManager_pressureLevel;
  #else
  return 0;
  #endif
}

uint32_t TouchManager_getCurrentPressDuration() {
  if (TouchManager_pressStartTime == 0) return 0;
  return millis() - TouchManager_pressStartTime;
}

void TouchManager_printGestureStats() {
  Serial.printf("[GESTURE] Stats: LongPress=%s, DoubleTap=%s, Pressure=%d/%d, PressDuration=%dms\n",
                TouchManager_longPressDetected ? "Active" : "Inactive",
                TouchManager_doubleTapDetected ? "Detected" : "None",
                TouchManager_getCurrentPressureLevel(), TOUCH_PRESSURE_THRESHOLD,
                TouchManager_getCurrentPressDuration());
}

#else // !ENABLE_ADVANCED_GESTURES

// Stub functions when advanced gestures are disabled
void TouchManager_handlePressStart(int16_t x, int16_t y, uint32_t timestamp) {}
void TouchManager_handleDrag(int16_t x, int16_t y, uint32_t timestamp) {}
void TouchManager_handleRelease(int16_t x, int16_t y, uint32_t timestamp) {}
bool TouchManager_isLongPressActive() { return false; }
bool TouchManager_isDoubleTapDetected() { return false; }
uint8_t TouchManager_getCurrentPressureLevel() { return 0; }
uint32_t TouchManager_getCurrentPressDuration() { return 0; }
void TouchManager_printGestureStats() {}

#endif // ENABLE_ADVANCED_GESTURES

// Missing functions for compatibility
bool TouchManager_isBurst() {
  // Return true if we've had multiple touches in a short period
  return (millis() - TouchManager_getLastTouchTime()) < 100;
}

bool TouchManager_isActive() {
  // Return true if touch was detected recently
  return (millis() - TouchManager_getLastTouchTime()) < 1000;
}