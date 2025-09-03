// ─────────────────────────────────────────────────────────────
// Frequency Scaling Manager - PHASE 3: Dynamic CPU Performance Optimization
// Automatically scales CPU frequency based on system activity and performance demands
// ─────────────────────────────────────────────────────────────

#if ENABLE_DYNAMIC_FREQ_SCALING

// ───── Frequency Scaling State ─────
// FreqScalingMode enum is now defined in the main .ino file

static FreqScalingMode FreqManager_currentMode = FREQ_MODE_NORMAL;
static uint32_t FreqManager_lastActivityTime = 0;
static uint32_t FreqManager_lastFreqChange = 0;
static uint32_t FreqManager_modeStartTime = 0;

// ───── Performance Monitoring ─────
static uint32_t FreqManager_totalHighModeTime = 0;
static uint32_t FreqManager_totalNormalModeTime = 0; 
static uint32_t FreqManager_totalLowModeTime = 0;
static uint16_t FreqManager_frequencyChanges = 0;

// ───── Activity Detection State ─────
static bool FreqManager_touchActive = false;
static bool FreqManager_networkActive = false;
static bool FreqManager_displayActive = false;
static uint32_t FreqManager_lastTouchActivity = 0;
static uint32_t FreqManager_lastNetworkActivity = 0;
static uint32_t FreqManager_lastDisplayActivity = 0;

// Forward declarations
bool DisplayManager_isScreenOn();
uint8_t NetworkTask_getQueueDepth();
bool TouchManager_isLongPressActive();

// ───── Core Frequency Management Functions ─────

bool FreqManager_setFrequency(FreqScalingMode mode) {
  uint32_t targetFreq;
  const char* modeName;
  
  switch (mode) {
    case FREQ_MODE_HIGH:
      targetFreq = CPU_FREQ_HIGH_MHZ * 1000000UL;
      modeName = "HIGH";
      break;
    case FREQ_MODE_NORMAL:  
      targetFreq = CPU_FREQ_NORMAL_MHZ * 1000000UL;
      modeName = "NORMAL";
      break;
    case FREQ_MODE_LOW:
      targetFreq = CPU_FREQ_LOW_MHZ * 1000000UL;
      modeName = "LOW";
      break;
    default:
      return false;
  }
  
  // Only change if different from current
  if (mode == FreqManager_currentMode) return true;
  
  uint32_t now = millis();
  uint32_t currentFreq = getCpuFrequencyMhz() * 1000000UL;
  
  // Update time spent in previous mode
  uint32_t timeInMode = now - FreqManager_modeStartTime;
  switch (FreqManager_currentMode) {
    case FREQ_MODE_HIGH:   FreqManager_totalHighModeTime += timeInMode; break;
    case FREQ_MODE_NORMAL: FreqManager_totalNormalModeTime += timeInMode; break; 
    case FREQ_MODE_LOW:    FreqManager_totalLowModeTime += timeInMode; break;
  }
  
  // Attempt frequency change
  if (setCpuFrequencyMhz(targetFreq / 1000000UL)) {
    FreqManager_currentMode = mode;
    FreqManager_lastFreqChange = now;
    FreqManager_modeStartTime = now;
    FreqManager_frequencyChanges++;
    
    Serial.printf("[FREQ] Changed: %dMHz -> %dMHz (%s mode) - change #%d\n",
                  currentFreq / 1000000UL, targetFreq / 1000000UL, 
                  modeName, FreqManager_frequencyChanges);
    return true;
  } else {
    Serial.printf("[FREQ] Failed to change frequency to %dMHz\n", targetFreq / 1000000UL);
    return false;
  }
}

// ───── Activity Detection Functions ─────

void FreqManager_notifyTouchActivity() {
  FreqManager_lastTouchActivity = millis();
  FreqManager_touchActive = true;
  FreqManager_lastActivityTime = FreqManager_lastTouchActivity;
}

void FreqManager_notifyNetworkActivity() {
  FreqManager_lastNetworkActivity = millis(); 
  FreqManager_networkActive = true;
  FreqManager_lastActivityTime = FreqManager_lastNetworkActivity;
}

void FreqManager_notifyDisplayActivity() {
  // CRITICAL FIX: Don't change frequency during display operations
  // This was causing SPI timing corruption and display artifacts
  static bool displayOperationActive = false;
  static uint32_t lastNotification = 0;
  
  uint32_t now = millis();
  FreqManager_lastDisplayActivity = now;
  FreqManager_displayActive = true;
  FreqManager_lastActivityTime = now;
  
  // Prevent frequency scaling during active display operations
  if (!displayOperationActive && (now - lastNotification) > 100) {
    displayOperationActive = true;
    // Lock to high performance mode during display activity
    if (FreqManager_currentMode != FREQ_MODE_HIGH) {
      FreqManager_setFrequency(FREQ_MODE_HIGH);
    }
    lastNotification = now;
    
    // Clear the lock after display operation window
    static uint32_t unlockTime = now + 200; // 200ms lock
    if (now > unlockTime) {
      displayOperationActive = false;
    }
  }
}

void FreqManager_notifyGeneralActivity() {
  FreqManager_lastActivityTime = millis();
}

// ───── Activity State Assessment ─────

bool FreqManager_isSystemActive() {
  uint32_t now = millis();
  
  // Check recent activity across all subsystems
  bool recentTouch = (now - FreqManager_lastTouchActivity < FREQ_SCALE_ACTIVITY_THRESHOLD_MS);
  bool recentNetwork = (now - FreqManager_lastNetworkActivity < FREQ_SCALE_ACTIVITY_THRESHOLD_MS); 
  bool recentDisplay = (now - FreqManager_lastDisplayActivity < FREQ_SCALE_ACTIVITY_THRESHOLD_MS);
  bool screenOn = DisplayManager_isScreenOn();
  
  // Check for active gestures or network queue
  bool touchGestures = TouchManager_isLongPressActive();
  bool networkQueue = (NetworkTask_getQueueDepth() > 0);
  
  return recentTouch || recentNetwork || recentDisplay || screenOn || touchGestures || networkQueue;
}

bool FreqManager_isHighPerformanceNeeded() {
  uint32_t now = millis();
  
  // High performance needed during active user interaction
  bool activeTouchGesture = TouchManager_isLongPressActive();
  bool recentTouch = (now - FreqManager_lastTouchActivity < 1000); // Very recent touch
  bool heavyNetworkLoad = (NetworkTask_getQueueDepth() > 3);
  bool screenJustTurnedOn = DisplayManager_isScreenOn() && (now - FreqManager_lastDisplayActivity < 3000);
  
  return activeTouchGesture || recentTouch || heavyNetworkLoad || screenJustTurnedOn;
}

// ───── Main Frequency Scaling Logic ─────

void FreqManager_update() {
  if (!ENABLE_DYNAMIC_FREQ_SCALING) return;
  
  uint32_t now = millis();
  static uint32_t lastUpdate = 0;
  
  // CRITICAL FIX: Increase update interval to prevent display corruption
  // Rapid frequency changes were causing SPI timing issues during display operations
  if (now - lastUpdate < 1000) return; // Increased from 500ms to 1000ms
  lastUpdate = now;
  
  // Update activity flags based on recent activity
  FreqManager_touchActive = (now - FreqManager_lastTouchActivity < FREQ_SCALE_ACTIVITY_THRESHOLD_MS);
  FreqManager_networkActive = (now - FreqManager_lastNetworkActivity < FREQ_SCALE_ACTIVITY_THRESHOLD_MS);
  FreqManager_displayActive = (now - FreqManager_lastDisplayActivity < FREQ_SCALE_ACTIVITY_THRESHOLD_MS);
  
  // CRITICAL FIX: Don't change frequency if display activity is recent
  // This prevents mid-render frequency changes that cause corruption
  if (FreqManager_displayActive) {
    // Keep high performance during display operations
    if (FreqManager_currentMode != FREQ_MODE_HIGH) {
      FreqManager_setFrequency(FREQ_MODE_HIGH);
    }
    return;
  }
  
  // Determine optimal frequency mode only when display is inactive
  FreqScalingMode targetMode = FREQ_MODE_NORMAL; // Default
  
  if (FreqManager_isHighPerformanceNeeded()) {
    targetMode = FREQ_MODE_HIGH;
  } else if (!FreqManager_isSystemActive() && 
             (now - FreqManager_lastActivityTime > FREQ_SCALE_INACTIVITY_THRESHOLD_MS)) {
    targetMode = FREQ_MODE_LOW;
  } else {
    targetMode = FREQ_MODE_NORMAL;
  }
  
  // Apply frequency change if needed (only when display inactive)
  FreqManager_setFrequency(targetMode);
}

// ───── Initialization and Management ─────

void FreqManager_init() {
  if (!ENABLE_DYNAMIC_FREQ_SCALING) {
    Serial.println("[FREQ] Dynamic frequency scaling disabled");
    return;
  }
  
  Serial.println("[FREQ] Initializing dynamic frequency scaling...");
  
  uint32_t currentFreq = getCpuFrequencyMhz();
  Serial.printf("[FREQ] Current CPU frequency: %dMHz\n", currentFreq);
  
  // Start in normal mode
  FreqManager_currentMode = FREQ_MODE_NORMAL;
  FreqManager_lastActivityTime = millis();
  FreqManager_modeStartTime = millis();
  
  // Set initial frequency to normal
  FreqManager_setFrequency(FREQ_MODE_NORMAL);
  
  Serial.printf("[FREQ] Frequency scaling initialized - modes: %dMHz/%dMHz/%dMHz\n",
                CPU_FREQ_HIGH_MHZ, CPU_FREQ_NORMAL_MHZ, CPU_FREQ_LOW_MHZ);
}

// ───── Status and Statistics Functions ─────

FreqScalingMode FreqManager_getCurrentMode() {
  return FreqManager_currentMode;
}

uint32_t FreqManager_getCurrentFrequencyMHz() {
  return getCpuFrequencyMhz();
}

const char* FreqManager_getModeString() {
  switch (FreqManager_currentMode) {
    case FREQ_MODE_HIGH:   return "HIGH";
    case FREQ_MODE_NORMAL: return "NORMAL"; 
    case FREQ_MODE_LOW:    return "LOW";
    default:               return "UNKNOWN";
  }
}

void FreqManager_printStats() {
  uint32_t uptime = millis();
  uint32_t totalTime = FreqManager_totalHighModeTime + FreqManager_totalNormalModeTime + FreqManager_totalLowModeTime;
  
  Serial.printf("[FREQ] Stats: Current=%dMHz (%s), Changes=%d\n",
                FreqManager_getCurrentFrequencyMHz(), FreqManager_getModeString(),
                FreqManager_frequencyChanges);
  
  if (totalTime > 0) {
    Serial.printf("[FREQ] Time distribution: HIGH=%.1f%% NORMAL=%.1f%% LOW=%.1f%%\n",
                  (FreqManager_totalHighModeTime * 100.0f) / totalTime,
                  (FreqManager_totalNormalModeTime * 100.0f) / totalTime,
                  (FreqManager_totalLowModeTime * 100.0f) / totalTime);
  }
  
  Serial.printf("[FREQ] Activity: Touch=%s Network=%s Display=%s\n",
                FreqManager_touchActive ? "Y" : "N",
                FreqManager_networkActive ? "Y" : "N", 
                FreqManager_displayActive ? "Y" : "N");
}

uint16_t FreqManager_getFrequencyChanges() {
  return FreqManager_frequencyChanges;
}

float FreqManager_getHighModePercentage() {
  uint32_t totalTime = FreqManager_totalHighModeTime + FreqManager_totalNormalModeTime + FreqManager_totalLowModeTime;
  return totalTime > 0 ? (FreqManager_totalHighModeTime * 100.0f) / totalTime : 0.0f;
}

// ───── Power Estimation Functions ─────

uint32_t FreqManager_estimatePowerSavingsMW() {
  // Rough power consumption estimates for ESP32-S3
  // These are approximate values - actual consumption varies by workload
  const uint16_t POWER_240MHZ_MW = 45;  // ~45mW at 240MHz
  const uint16_t POWER_160MHZ_MW = 35;  // ~35mW at 160MHz  
  const uint16_t POWER_80MHZ_MW = 25;   // ~25mW at 80MHz
  
  // Calculate power that would be consumed if always at 240MHz
  uint32_t totalTime = FreqManager_totalHighModeTime + FreqManager_totalNormalModeTime + FreqManager_totalLowModeTime;
  if (totalTime == 0) return 0;
  
  uint32_t wouldBeHighPower = (totalTime * POWER_240MHZ_MW) / 1000; // mW*ms -> mJ
  
  // Calculate actual power consumption  
  uint32_t actualPower = ((FreqManager_totalHighModeTime * POWER_240MHZ_MW) +
                         (FreqManager_totalNormalModeTime * POWER_160MHZ_MW) +
                         (FreqManager_totalLowModeTime * POWER_80MHZ_MW)) / 1000;
  
  return (wouldBeHighPower > actualPower) ? (wouldBeHighPower - actualPower) : 0;
}

// Missing functions for compatibility
uint8_t FreqManager_getCurrentFrequency() { 
  // Return frequency as scale 0-3 (LOW=0, NORMAL=1, HIGH=2, MAX=3)
  uint32_t mhz = FreqManager_getCurrentFrequencyMHz();
  if (mhz <= 80) return 0;
  else if (mhz <= 160) return 1;
  else if (mhz <= 240) return 2;
  else return 3;
}
void FreqManager_setFrequency(uint8_t freq) { /* Implementation not needed for this version */ }
uint32_t FreqManager_getCPULoad() { return 50; } // Return a reasonable default
void FreqManager_notifyActivityType(uint8_t activityType) { /* Log activity for optimization */ }

#else // !ENABLE_DYNAMIC_FREQ_SCALING

// ───── Stub Functions When Frequency Scaling is Disabled ─────

void FreqManager_init() {}
void FreqManager_update() {}
void FreqManager_notifyTouchActivity() {}
void FreqManager_notifyNetworkActivity() {}
void FreqManager_notifyDisplayActivity() {}
void FreqManager_notifyGeneralActivity() {}
bool FreqManager_isSystemActive() { return true; }
uint32_t FreqManager_getCurrentFrequencyMHz() { return getCpuFrequencyMhz(); }
const char* FreqManager_getModeString() { return "FIXED"; }
void FreqManager_printStats() { Serial.println("[FREQ] Dynamic frequency scaling disabled"); }
uint16_t FreqManager_getFrequencyChanges() { return 0; }
float FreqManager_getHighModePercentage() { return 100.0f; }
uint32_t FreqManager_estimatePowerSavingsMW() { return 0; }

// Missing functions for compatibility
uint8_t FreqManager_getCurrentFrequency() { 
  // Return frequency as scale 0-3 (LOW=0, NORMAL=1, HIGH=2, MAX=3)
  uint32_t mhz = getCpuFrequencyMhz();
  if (mhz <= 80) return 0;
  else if (mhz <= 160) return 1;
  else if (mhz <= 240) return 2;
  else return 3;
}
void FreqManager_setFrequency(uint8_t freq) { /* Stub - no dynamic scaling */ }
uint32_t FreqManager_getCPULoad() { return 50; } // Return a reasonable default
void FreqManager_notifyActivityType(uint8_t activityType) { /* Stub - no activity tracking */ }

#endif // ENABLE_DYNAMIC_FREQ_SCALING