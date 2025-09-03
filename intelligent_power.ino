// ─────────────────────────────────────────────────────────────
// Intelligent Power Manager - PHASE 4: Advanced Power States & Predictive Management
// Multi-tier power management with usage pattern learning and deep sleep optimization
// ─────────────────────────────────────────────────────────────

#if ENABLE_INTELLIGENT_POWER

#include <esp_sleep.h>
#include <esp_pm.h>
#include <soc/rtc.h>

// ───── Power State Definitions ─────
// IntelligentPowerState enum is now defined in the main .ino file

enum WakeupReason {
  WAKE_TOUCH = 1,
  WAKE_NETWORK = 2,
  WAKE_TIMER = 3,
  WAKE_PREDICTED = 4,
  WAKE_EXTERNAL = 5
};

// ───── Power Context Structure ─────
typedef struct {
  IntelligentPowerState currentState;
  IntelligentPowerState previousState;
  uint32_t stateEntryTime;
  uint32_t lastActivity;
  uint32_t lastTouchActivity;
  uint32_t lastNetworkActivity;
  uint32_t totalUptime;
  uint32_t totalSleepTime;
  uint32_t batteryLevel;
  float batteryDrainRate;
  bool externalPowerConnected;
  bool wakeOnTouch;
  bool wakeOnNetwork;
  bool adaptivePowerEnabled;
} IntelligentPowerContext;

// ───── Usage Pattern Learning ─────
#define MAX_INTERACTION_HISTORY 48
#define MAX_DAILY_PATTERNS 12

typedef struct {
  uint32_t timestamp;
  uint16_t duration;
  uint8_t intensity;    // 1-10 scale of interaction intensity
} InteractionEvent;

typedef struct {
  uint8_t hourOfDay;
  uint16_t typicalDuration;
  uint8_t frequency;
  uint16_t avgIntensity;
  bool active;
} DailyPattern;

typedef struct {
  InteractionEvent history[MAX_INTERACTION_HISTORY];
  uint8_t historyIndex;
  DailyPattern dailyPatterns[MAX_DAILY_PATTERNS];
  uint32_t totalInteractions;
  uint32_t avgSessionDuration;
  uint32_t predictedNextInteraction;
  bool patternLearned;
  float confidenceScore;
} UsagePattern;

// ───── Global Power State ─────
static IntelligentPowerContext IPower_context = {
  .currentState = IPOWER_ACTIVE,
  .previousState = IPOWER_ACTIVE,
  .stateEntryTime = 0,
  .lastActivity = 0,
  .lastTouchActivity = 0,
  .lastNetworkActivity = 0,
  .totalUptime = 0,
  .totalSleepTime = 0,
  .batteryLevel = 100,
  .batteryDrainRate = 0.0f,
  .externalPowerConnected = false,
  .wakeOnTouch = true,
  .wakeOnNetwork = true,
  .adaptivePowerEnabled = true
};
static UsagePattern IPower_usagePattern = {0};
static bool IPower_initialized = false;
static uint32_t IPower_lastStateChange = 0;
static uint32_t IPower_lastDeepSleep = 0;

// Forward declarations
void FreqManager_setFrequency(uint8_t mode);
void DisplayManager_enterStandby();
void DisplayManager_exitStandby();
bool TouchManager_isActive();
bool NetworkTask_hasActivity();
uint32_t PowerManager_getBatteryLevel();
bool PowerManager_isExternalPower();
void TwistManager_enterPowerSave();
void TwistManager_exitPowerSave();
void WiFiManager_enterPowerSave();
void WiFiManager_exitPowerSave();
void FreqManager_notifyActivityType(uint8_t type);

// ───── Power State Management ─────
const char* IPower_getStateName(IntelligentPowerState state) {
  switch (state) {
    case IPOWER_ACTIVE: return "ACTIVE";
    case IPOWER_INTERACTIVE: return "INTERACTIVE";
    case IPOWER_IDLE: return "IDLE";
    case IPOWER_STANDBY: return "STANDBY";
    case IPOWER_DEEP_SLEEP: return "DEEP_SLEEP";
    default: return "UNKNOWN";
  }
}

void IPower_transitionToState(IntelligentPowerState newState) {
  if (newState == IPower_context.currentState) return;
  
  IntelligentPowerState oldState = IPower_context.currentState;
  uint32_t now = millis();
  
  Serial.printf("[IPOWER] State transition: %s -> %s\n", 
                IPower_getStateName(oldState), IPower_getStateName(newState));
  
  // Exit old state
  switch (oldState) {
    case IPOWER_STANDBY:
      DisplayManager_exitStandby();
      TwistManager_exitPowerSave();
      break;
    case IPOWER_DEEP_SLEEP:
      // Deep sleep exit handled by wake routine
      break;
  }
  
  // Enter new state
  switch (newState) {
    case IPOWER_ACTIVE:
      FreqManager_setFrequency(2); // High performance mode
      break;
      
    case IPOWER_INTERACTIVE:
      FreqManager_setFrequency(1); // Normal performance mode
      break;
      
    case IPOWER_IDLE:
      FreqManager_setFrequency(0); // Low performance mode
      break;
      
    case IPOWER_STANDBY:
      DisplayManager_enterStandby();
      TwistManager_enterPowerSave();
      FreqManager_setFrequency(0); // Minimum performance
      
      // Reduce WiFi power if on battery
      if (!IPower_context.externalPowerConnected) {
        WiFiManager_enterPowerSave();
      }
      break;
      
    case IPOWER_DEEP_SLEEP:
      IPower_enterDeepSleep();
      return; // Deep sleep doesn't return normally
  }
  
  // Update context
  IPower_context.previousState = oldState;
  IPower_context.currentState = newState;
  IPower_context.stateEntryTime = now;
  IPower_lastStateChange = now;
  
  // Notify frequency manager of state change
  FreqManager_notifyActivityType(newState);
}

// ───── Usage Pattern Learning ─────
void IPower_recordInteraction(uint32_t duration, uint8_t intensity) {
  // Prevent spam by ignoring similar consecutive interactions
  static uint32_t lastDuration = 0;
  static uint8_t lastIntensity = 0;
  static uint32_t lastRecordTime = 0;
  uint32_t now = millis();
  
  // If this is very similar to the last interaction and it's within 1 second, ignore it
  if (abs((int32_t)duration - (int32_t)lastDuration) < 1000 && 
      intensity == lastIntensity && 
      (now - lastRecordTime) < 1000) {
    return;
  }
  
  lastDuration = duration;
  lastIntensity = intensity;
  lastRecordTime = now;
  
  UsagePattern* pattern = &IPower_usagePattern;
  
  // Add to interaction history
  InteractionEvent* event = &pattern->history[pattern->historyIndex];
  event->timestamp = now;
  event->duration = min(duration, (uint32_t)65535);
  event->intensity = min(intensity, (uint8_t)10);
  
  pattern->historyIndex = (pattern->historyIndex + 1) % MAX_INTERACTION_HISTORY;
  pattern->totalInteractions++;
  
  // Update running averages
  pattern->avgSessionDuration = (pattern->avgSessionDuration * 7 + duration) / 8;
  
  // Learn daily patterns
  time_t rawtime = now / 1000;
  struct tm* timeinfo = localtime(&rawtime);
  
  // Safety check for null pointer from localtime()
  if (!timeinfo) {
    Serial.println("[IPOWER] ERROR: localtime() returned null");
    return; // Skip pattern learning if time is invalid
  }
  
  uint8_t currentHour = timeinfo->tm_hour;
  
  // Find or create daily pattern for this hour
  DailyPattern* dayPattern = nullptr;
  for (int i = 0; i < MAX_DAILY_PATTERNS; i++) {
    if (pattern->dailyPatterns[i].hourOfDay == currentHour && pattern->dailyPatterns[i].active) {
      dayPattern = &pattern->dailyPatterns[i];
      break;
    }
  }
  
  if (!dayPattern) {
    // Find empty slot
    for (int i = 0; i < MAX_DAILY_PATTERNS; i++) {
      if (!pattern->dailyPatterns[i].active) {
        dayPattern = &pattern->dailyPatterns[i];
        dayPattern->hourOfDay = currentHour;
        dayPattern->active = true;
        break;
      }
    }
  }
  
  if (dayPattern) {
    dayPattern->frequency++;
    dayPattern->typicalDuration = (dayPattern->typicalDuration * 3 + duration) / 4;
    dayPattern->avgIntensity = (dayPattern->avgIntensity * 3 + intensity * 100) / 4;
  }
  
  // Update pattern confidence
  if (pattern->totalInteractions > 10) {
    float consistency = IPower_calculatePatternConsistency();
    pattern->confidenceScore = consistency;
    pattern->patternLearned = consistency > 0.7f;
  }
  
  // Rate-limited logging to prevent spam
  static uint32_t lastLogTime = 0;
  static uint32_t logCount = 0;
  uint32_t currentTime = millis();
  
  if (currentTime - lastLogTime > 10000) { // Log only every 10 seconds
    if (logCount > 0) {
      Serial.printf("[IPOWER] Recorded %d interactions (last: duration=%dms, intensity=%d, patterns=%s)\n",
                    logCount, duration, intensity, pattern->patternLearned ? "learned" : "learning");
    }
    lastLogTime = currentTime;
    logCount = 0;
  }
  logCount++;
}

float IPower_calculatePatternConsistency() {
  UsagePattern* pattern = &IPower_usagePattern;
  
  if (pattern->totalInteractions < 10) return 0.0f;
  
  // Calculate variance in interaction intervals
  uint32_t intervals[MAX_INTERACTION_HISTORY - 1];
  uint8_t validIntervals = 0;
  uint32_t totalInterval = 0;
  
  for (int i = 1; i < MAX_INTERACTION_HISTORY; i++) {
    uint32_t prev = pattern->history[(i - 1 + pattern->historyIndex) % MAX_INTERACTION_HISTORY].timestamp;
    uint32_t curr = pattern->history[(i + pattern->historyIndex) % MAX_INTERACTION_HISTORY].timestamp;
    
    if (curr > prev && (curr - prev) < 86400000) { // Less than 24 hours
      intervals[validIntervals] = curr - prev;
      totalInterval += intervals[validIntervals];
      validIntervals++;
    }
  }
  
  if (validIntervals < 5) return 0.0f;
  
  uint32_t avgInterval = totalInterval / validIntervals;
  uint64_t variance = 0;
  
  for (int i = 0; i < validIntervals; i++) {
    int32_t diff = intervals[i] - avgInterval;
    variance += diff * diff;
  }
  
  variance /= validIntervals;
  float standardDev = sqrt(variance);
  
  // Lower variance = higher consistency
  float consistency = 1.0f - min(standardDev / avgInterval, 1.0f);
  
  return consistency;
}

uint32_t IPower_predictNextInteraction() {
  UsagePattern* pattern = &IPower_usagePattern;
  
  if (!pattern->patternLearned || pattern->totalInteractions < 10) {
    return 0; // Cannot predict
  }
  
  time_t rawtime = millis() / 1000;
  struct tm* timeinfo = localtime(&rawtime);
  uint8_t currentHour = timeinfo->tm_hour;
  uint8_t nextHour = (currentHour + 1) % 24;
  
  // Find pattern for next hour
  DailyPattern* nextPattern = nullptr;
  for (int i = 0; i < MAX_DAILY_PATTERNS; i++) {
    if (pattern->dailyPatterns[i].hourOfDay == nextHour && pattern->dailyPatterns[i].active) {
      nextPattern = &pattern->dailyPatterns[i];
      break;
    }
  }
  
  if (nextPattern && nextPattern->frequency > 2) {
    // Predict interaction in next hour based on historical frequency
    uint32_t hourStart = (rawtime / 3600) * 3600 + 3600; // Start of next hour
    uint32_t estimatedTime = hourStart + (3600 * nextPattern->frequency / 10); // Rough estimate within hour
    
    pattern->predictedNextInteraction = estimatedTime * 1000; // Convert to milliseconds
    return pattern->predictedNextInteraction;
  }
  
  return 0;
}

// ───── Deep Sleep Management ─────
void IPower_enterDeepSleep() {
  Serial.println("[IPOWER] Entering deep sleep mode...");
  
  uint32_t now = millis();
  IPower_context.totalUptime += now - IPower_context.stateEntryTime;
  IPower_lastDeepSleep = now;
  
  // Configure wake sources
  if (IPower_context.wakeOnTouch) {
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_TP_INT, 0); // Wake on touch interrupt
  }
  
  // Calculate wake timer based on predicted interaction
  uint32_t predictedWake = IPower_predictNextInteraction();
  if (predictedWake > 0 && predictedWake > now) {
    uint32_t sleepDuration = min(predictedWake - now, 7200000UL); // Max 2 hours
    esp_sleep_enable_timer_wakeup(sleepDuration * 1000UL); // Convert to microseconds
    
    Serial.printf("[IPOWER] Predictive wake in %d minutes\n", sleepDuration / 60000);
  } else {
    // Default wake timer - 1 hour max
    esp_sleep_enable_timer_wakeup(3600000000ULL); // 1 hour in microseconds
  }
  
  // Configure light sleep mode if external power connected
  if (IPower_context.externalPowerConnected) {
    esp_pm_config_esp32s3_t pm_config = {
      .max_freq_mhz = 80,
      .min_freq_mhz = 10,
      .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
    
    Serial.println("[IPOWER] Configured light sleep (external power)");
  }
  
  // Save critical state before sleep
  IPower_context.currentState = IPOWER_DEEP_SLEEP;
  
  // Enter deep sleep
  Serial.println("[IPOWER] Entering sleep now...");
  Serial.flush();
  delay(100); // Ensure serial output completes
  
  esp_deep_sleep_start();
}

void IPower_handleWakeup() {
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  uint32_t now = millis();
  
  // Calculate sleep duration
  uint32_t sleepDuration = now - IPower_lastDeepSleep;
  IPower_context.totalSleepTime += sleepDuration;
  
  WakeupReason reason = WAKE_TIMER;
  const char* reasonStr = "TIMER";
  
  switch (wakeupReason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      reason = WAKE_TOUCH;
      reasonStr = "TOUCH";
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      reason = WAKE_EXTERNAL;
      reasonStr = "EXTERNAL";
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      // Check if this was a predicted wake
      if (abs((int32_t)(now - IPower_usagePattern.predictedNextInteraction)) < 60000) {
        reason = WAKE_PREDICTED;
        reasonStr = "PREDICTED";
      }
      break;
    case ESP_SLEEP_WAKEUP_UART:
      reason = WAKE_NETWORK;
      reasonStr = "NETWORK";
      break;
  }
  
  Serial.printf("[IPOWER] Wake from deep sleep: %s (slept %dms)\n", reasonStr, sleepDuration);
  
  // Transition to appropriate wake state
  if (reason == WAKE_TOUCH) {
    IPower_transitionToState(IPOWER_ACTIVE);
    IPower_recordInteraction(0, 8); // High intensity interaction
  } else if (reason == WAKE_PREDICTED) {
    IPower_transitionToState(IPOWER_INTERACTIVE);
  } else {
    IPower_transitionToState(IPOWER_IDLE);
  }
  
  IPower_context.lastActivity = now;
}

// ───── Main Power Management Logic ─────
void IPower_updateStateMachine() {
  if (!IPower_context.adaptivePowerEnabled) return;
  
  uint32_t now = millis();
  uint32_t idleTime = now - IPower_context.lastActivity;
  uint32_t stateTime = now - IPower_context.stateEntryTime;
  
  // Update activity timestamps
  if (TouchManager_isActive()) {
    IPower_context.lastActivity = now;
    IPower_context.lastTouchActivity = now;
    idleTime = 0;
  }
  
  if (NetworkTask_hasActivity()) {
    IPower_context.lastNetworkActivity = now;
    // Network activity is less significant than touch for power decisions
  }
  
  // State machine transitions
  switch (IPower_context.currentState) {
    case IPOWER_ACTIVE:
      if (idleTime > 5000) { // 5s idle -> interactive
        IPower_transitionToState(IPOWER_INTERACTIVE);
      }
      break;
      
    case IPOWER_INTERACTIVE:
      if (idleTime > 15000) { // 15s idle -> idle
        IPower_transitionToState(IPOWER_IDLE);
      } else if (idleTime < 1000) { // Activity -> active
        IPower_transitionToState(IPOWER_ACTIVE);
      }
      break;
      
    case IPOWER_IDLE:
      if (idleTime > 45000) { // 45s idle -> standby
        IPower_transitionToState(IPOWER_STANDBY);
      } else if (idleTime < 1000) { // Activity -> active
        IPower_transitionToState(IPOWER_ACTIVE);
      }
      break;
      
    case IPOWER_STANDBY:
      {
        // Determine deep sleep threshold based on power source and battery level
        uint32_t deepSleepThreshold = IPower_context.externalPowerConnected ? 600000 : 120000; // 10min vs 2min
        
        // Adjust threshold based on battery level
        if (IPower_context.batteryLevel < 20) {
          deepSleepThreshold = 60000; // 1min on low battery
        } else if (IPower_context.batteryLevel < 50) {
          deepSleepThreshold = deepSleepThreshold / 2; // Faster sleep on medium battery
        }
        
        if (idleTime > deepSleepThreshold) {
          // Check if we should predict wake time
          uint32_t predictedWake = IPower_predictNextInteraction();
          if (predictedWake > 0 && (predictedWake - now) < deepSleepThreshold) {
            Serial.printf("[IPOWER] Delaying deep sleep for predicted interaction in %dms\n", 
                          predictedWake - now);
          } else {
            IPower_transitionToState(IPOWER_DEEP_SLEEP);
          }
        } else if (idleTime < 1000) { // Activity -> active
          IPower_transitionToState(IPOWER_ACTIVE);
        }
        break;
      }
      
    case IPOWER_DEEP_SLEEP:
      // Should not reach here in normal operation
      break;
  }
  
  // Record interaction sessions for learning
  if (IPower_context.currentState == IPOWER_ACTIVE && 
      IPower_context.previousState != IPOWER_ACTIVE && 
      stateTime > 5000) {
    // Calculate interaction intensity based on frequency and duration
    uint32_t calculated = max((uint32_t)1, (stateTime / 10000) + (TouchManager_isActive() ? 5 : 0));
    uint8_t intensity = min((uint8_t)10, (uint8_t)calculated);
    IPower_recordInteraction(stateTime, intensity);
  }
}

// ───── Core Functions ─────
void IPower_init() {
  if (!ENABLE_INTELLIGENT_POWER) {
    Serial.println("[IPOWER] Intelligent power management disabled");
    return;
  }
  
  Serial.println("[IPOWER] Initializing intelligent power management...");
  
  // Initialize context
  IPower_context.currentState = IPOWER_ACTIVE;
  IPower_context.previousState = IPOWER_ACTIVE;
  IPower_context.stateEntryTime = millis();
  IPower_context.lastActivity = millis();
  IPower_context.wakeOnTouch = true;
  IPower_context.wakeOnNetwork = false;
  IPower_context.adaptivePowerEnabled = true;
  IPower_context.batteryLevel = PowerManager_getBatteryLevel();
  IPower_context.externalPowerConnected = PowerManager_isExternalPower();
  
  // Initialize usage pattern
  memset(&IPower_usagePattern, 0, sizeof(UsagePattern));
  
  // Handle wake from deep sleep
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED) {
    IPower_handleWakeup();
  }
  
  IPower_initialized = true;
  Serial.println("[IPOWER] Intelligent power management initialized");
}

void IPower_update() {
  if (!ENABLE_INTELLIGENT_POWER || !IPower_initialized) return;
  
  // Update power context
  IPower_context.batteryLevel = PowerManager_getBatteryLevel();
  IPower_context.externalPowerConnected = PowerManager_isExternalPower();
  
  // Run state machine
  IPower_updateStateMachine();
  
  // Update battery drain rate calculation
  static uint32_t lastBatteryUpdate = 0;
  static uint32_t lastBatteryLevel = 100;
  
  if (millis() - lastBatteryUpdate > 60000) { // Update every minute
    if (!IPower_context.externalPowerConnected) {
      int32_t batteryDrop = lastBatteryLevel - IPower_context.batteryLevel;
      if (batteryDrop > 0) {
        IPower_context.batteryDrainRate = batteryDrop / 60.0f; // %/minute
      }
    }
    lastBatteryLevel = IPower_context.batteryLevel;
    lastBatteryUpdate = millis();
  }
}

// ───── Status and Control Functions ─────
IntelligentPowerState IPower_getCurrentState() {
  return ENABLE_INTELLIGENT_POWER ? IPower_context.currentState : IPOWER_ACTIVE;
}

bool IPower_isAdaptivePowerEnabled() {
  return ENABLE_INTELLIGENT_POWER && IPower_context.adaptivePowerEnabled;
}

void IPower_setAdaptivePower(bool enabled) {
  IPower_context.adaptivePowerEnabled = enabled;
  Serial.printf("[IPOWER] Adaptive power %s\n", enabled ? "enabled" : "disabled");
}

void IPower_forceState(IntelligentPowerState state) {
  if (state != IPower_context.currentState) {
    Serial.printf("[IPOWER] Force state: %s\n", IPower_getStateName(state));
    IPower_transitionToState(state);
  }
}

void IPower_notifyActivity() {
  IPower_context.lastActivity = millis();
  
  if (IPower_context.currentState == IPOWER_STANDBY || 
      IPower_context.currentState == IPOWER_IDLE) {
    IPower_transitionToState(IPOWER_ACTIVE);
  }
}

// ───── Statistics and Reporting ─────
void IPower_printStats() {
  if (!ENABLE_INTELLIGENT_POWER) {
    Serial.println("[IPOWER] Intelligent power disabled");
    return;
  }
  
  uint32_t now = millis();
  uint32_t uptime = IPower_context.totalUptime + (now - IPower_context.stateEntryTime);
  
  Serial.printf("[IPOWER] State: %s (for %dms)\n", 
                IPower_getStateName(IPower_context.currentState),
                now - IPower_context.stateEntryTime);
  Serial.printf("[IPOWER] Total uptime: %ds, sleep time: %ds\n",
                uptime / 1000, IPower_context.totalSleepTime / 1000);
  Serial.printf("[IPOWER] Battery: %d%%, drain rate: %.2f%%/min\n",
                IPower_context.batteryLevel, IPower_context.batteryDrainRate);
  Serial.printf("[IPOWER] Pattern learned: %s, confidence: %.2f\n",
                IPower_usagePattern.patternLearned ? "Yes" : "No",
                IPower_usagePattern.confidenceScore);
  
  if (IPower_usagePattern.predictedNextInteraction > 0) {
    int32_t timeToNext = IPower_usagePattern.predictedNextInteraction - now;
    Serial.printf("[IPOWER] Next predicted interaction: %ds\n", timeToNext / 1000);
  }
}

float IPower_getBatteryDrainRate() {
  return IPower_context.batteryDrainRate;
}

uint32_t IPower_getTotalUptime() {
  uint32_t now = millis();
  return IPower_context.totalUptime + (now - IPower_context.stateEntryTime);
}

uint32_t IPower_getTotalSleepTime() {
  return IPower_context.totalSleepTime;
}

bool IPower_hasLearnedPattern() {
  return IPower_usagePattern.patternLearned;
}

#else // !ENABLE_INTELLIGENT_POWER

// ───── Stub Functions When Intelligent Power is Disabled ─────
void IPower_init() {}
void IPower_update() {}
IntelligentPowerState IPower_getCurrentState() { return IPOWER_ACTIVE; }
bool IPower_isAdaptivePowerEnabled() { return false; }
void IPower_setAdaptivePower(bool enabled) {}
void IPower_forceState(IntelligentPowerState state) {}
void IPower_notifyActivity() {}
void IPower_printStats() { Serial.println("[IPOWER] Intelligent power disabled"); }
float IPower_getBatteryDrainRate() { return 0.0f; }
uint32_t IPower_getTotalUptime() { return millis(); }
uint32_t IPower_getTotalSleepTime() { return 0; }
bool IPower_hasLearnedPattern() { return false; }

#endif // ENABLE_INTELLIGENT_POWER