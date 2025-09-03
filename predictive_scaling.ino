// ─────────────────────────────────────────────────────────────
// Predictive Performance Scaling - PHASE 4: AI-Driven Performance Optimization
// Learns usage patterns and predicts performance needs for proactive scaling
// ─────────────────────────────────────────────────────────────

#if ENABLE_PREDICTIVE_SCALING

// ───── Predictive Scaling Configuration ─────
#define PREDICTION_HISTORY_SIZE 32          // Track last 32 performance events
#define PATTERN_ANALYSIS_INTERVAL_MS 10000  // Analyze patterns every 10s
#define SCALING_PREDICTION_ACCURACY 0.75f   // 75% confidence required for proactive scaling
#define MAX_PREDICTION_PATTERNS 16          // Maximum learned patterns
#define PERFORMANCE_SAMPLE_INTERVAL_MS 500  // Sample performance every 500ms

// ───── Performance Event Types ─────
// PerformanceEventType enum is now defined in the main .ino file

enum PredictionConfidence {
  PRED_LOW = 1,      // 0-50% confidence
  PRED_MEDIUM = 2,   // 50-75% confidence
  PRED_HIGH = 3,     // 75-90% confidence
  PRED_VERY_HIGH = 4 // 90%+ confidence
};

// ───── Performance Event Structure ─────
typedef struct {
  PerformanceEventType type;
  uint32_t timestamp;
  uint16_t duration;         // Duration in milliseconds
  uint8_t intensity;         // Performance intensity (1-10 scale)
  uint8_t cpuFreqRequired;   // Frequency mode required (0=low, 1=normal, 2=high)
  uint16_t memoryUsage;      // Peak memory usage during event
} PerformanceEvent;

// ───── Predictive Pattern Structure ─────
typedef struct {
  PerformanceEventType triggerType;  // What event type triggers this pattern
  PerformanceEventType targetType;   // What event type is predicted
  uint32_t avgDelay;                 // Average delay between trigger and target
  uint32_t minDelay;                 // Minimum observed delay
  uint32_t maxDelay;                 // Maximum observed delay
  uint8_t frequency;                 // How often this pattern occurs
  float accuracy;                    // Historical prediction accuracy
  uint32_t lastSeen;                 // When this pattern was last observed
  bool active;                       // Whether this pattern is being used
} PredictivePattern;

// ───── System Performance Context ─────
typedef struct {
  uint32_t currentLoad;          // Current system load percentage
  uint32_t avgLoad;              // Rolling average system load
  uint32_t peakLoad;             // Peak system load in current session
  uint32_t memoryPressure;       // Current memory pressure (0-100)
  float batteryDrainRate;        // Battery drain rate factor
  bool thermalThrottling;        // Whether thermal throttling is active
  uint32_t lastScalingDecision;  // Timestamp of last scaling decision
  uint8_t consecutiveOverloads;  // Count of consecutive overload events
} SystemPerformanceContext;

// ───── Predictive Scaling State ─────
typedef struct {
  PerformanceEvent eventHistory[PREDICTION_HISTORY_SIZE];
  uint8_t historyIndex;
  PredictivePattern patterns[MAX_PREDICTION_PATTERNS];
  uint8_t patternCount;
  SystemPerformanceContext systemContext;
  uint32_t totalPredictions;
  uint32_t correctPredictions;
  float overallAccuracy;
  bool adaptiveScalingEnabled;
  uint32_t lastPatternAnalysis;
  uint8_t currentPredictedFreq;
  uint32_t predictionValidUntil;
} PredictiveScalingContext;

// ───── Global State ─────
static PredictiveScalingContext PScale_context = {
  .eventHistory = {},
  .historyIndex = 0,
  .patterns = {},
  .patternCount = 0,
  .systemContext = {},
  .totalPredictions = 0,
  .correctPredictions = 0,
  .overallAccuracy = 0.0f,
  .adaptiveScalingEnabled = true,
  .lastPatternAnalysis = 0,
  .currentPredictedFreq = 0,
  .predictionValidUntil = 0
};
static bool PScale_initialized = false;
static uint32_t PScale_lastPerformanceSample = 0;

// Forward declarations
void FreqManager_setFrequency(uint8_t mode);
uint8_t FreqManager_getCurrentFrequency();
uint32_t FreqManager_getCPULoad();
uint32_t MemoryManager_getMemoryPressure();
bool TouchManager_isActive();
bool TouchManager_isBurst();
bool NetworkTask_hasActivity();
bool NetworkTask_isBurst();
bool DisplayManager_isIntensiveRender();
bool AnimationManager_isAnyAnimationActive();
uint32_t PowerManager_getBatteryLevel();
void Profiler_startTiming(uint8_t pointIndex);
void Profiler_endTiming(uint8_t pointIndex);

// ───── Performance Event Recording ─────
void PScale_recordEvent(PerformanceEventType type, uint16_t duration, uint8_t intensity) {
  if (!PScale_initialized) return;
  
  // Prevent spam by ignoring identical consecutive events
  static PerformanceEventType lastType = (PerformanceEventType)-1;
  static uint16_t lastDuration = 0;
  static uint8_t lastIntensity = 0;
  static uint32_t lastEventTime = 0;
  uint32_t currentTime = millis();
  
  // If this is identical to the last event and it's within 100ms, ignore it
  if (type == lastType && duration == lastDuration && intensity == lastIntensity && 
      (currentTime - lastEventTime) < 100) {
    return;
  }
  
  lastType = type;
  lastDuration = duration;
  lastIntensity = intensity;
  lastEventTime = currentTime;
  
  PerformanceEvent* event = &PScale_context.eventHistory[PScale_context.historyIndex];
  
  event->type = type;
  event->timestamp = millis();
  event->duration = duration;
  event->intensity = min(intensity, (uint8_t)10);
  event->memoryUsage = MemoryManager_getMemoryPressure();
  
  // Determine required CPU frequency based on event characteristics
  if (intensity >= 8 || duration > 500) {
    event->cpuFreqRequired = 2; // High frequency
  } else if (intensity >= 5 || duration > 200) {
    event->cpuFreqRequired = 1; // Normal frequency
  } else {
    event->cpuFreqRequired = 0; // Low frequency acceptable
  }
  
  PScale_context.historyIndex = (PScale_context.historyIndex + 1) % PREDICTION_HISTORY_SIZE;
  
  // Rate-limited logging to prevent spam
  static uint32_t lastLogTime = 0;
  static uint32_t logCount = 0;
  uint32_t now = millis();
  
  if (now - lastLogTime > 5000) { // Log only every 5 seconds
    if (logCount > 0) {
      Serial.printf("[PSCALE] Recorded %d events (last: type=%d, duration=%dms, intensity=%d, freq_req=%d)\n",
                    logCount, type, duration, intensity, event->cpuFreqRequired);
    }
    lastLogTime = now;
    logCount = 0;
  }
  logCount++;
  
  // Immediate reactive scaling if needed
  if (event->cpuFreqRequired > FreqManager_getCurrentFrequency()) {
    Serial.printf("[PSCALE] Reactive scaling to mode %d\n", event->cpuFreqRequired);
    FreqManager_setFrequency(event->cpuFreqRequired);
  }
}

// ───── Pattern Learning and Analysis ─────
void PScale_analyzePatterns() {
  if (!PScale_initialized || PScale_context.historyIndex < 10) return;
  
  uint32_t now = millis();
  if (now - PScale_context.lastPatternAnalysis < PATTERN_ANALYSIS_INTERVAL_MS) return;
  
  PScale_context.lastPatternAnalysis = now;
  
  Serial.println("[PSCALE] Analyzing performance patterns...");
  
  // Look for temporal correlations in event history
  for (int i = 1; i < PREDICTION_HISTORY_SIZE; i++) {
    if (PScale_context.eventHistory[i].timestamp == 0) continue;
    
    PerformanceEvent* current = &PScale_context.eventHistory[i];
    
    // Look for preceding events that might be predictive triggers
    for (int j = 0; j < i; j++) {
      PerformanceEvent* prev = &PScale_context.eventHistory[j];
      if (prev->timestamp == 0) continue;
      
      uint32_t delay = current->timestamp - prev->timestamp;
      
      // Consider events within 5 seconds as potentially correlated
      if (delay > 0 && delay < 5000) {
        PScale_learnPattern(prev->type, current->type, delay);
      }
    }
  }
  
  // Clean up old patterns with low accuracy
  PScale_prunePatterns();
}

void PScale_learnPattern(PerformanceEventType triggerType, PerformanceEventType targetType, uint32_t delay) {
  // Find existing pattern or create new one
  PredictivePattern* pattern = nullptr;
  
  for (uint8_t i = 0; i < PScale_context.patternCount; i++) {
    if (PScale_context.patterns[i].triggerType == triggerType && 
        PScale_context.patterns[i].targetType == targetType) {
      pattern = &PScale_context.patterns[i];
      break;
    }
  }
  
  if (!pattern && PScale_context.patternCount < MAX_PREDICTION_PATTERNS) {
    pattern = &PScale_context.patterns[PScale_context.patternCount];
    pattern->triggerType = triggerType;
    pattern->targetType = targetType;
    pattern->frequency = 0;
    pattern->avgDelay = 0;
    pattern->minDelay = UINT32_MAX;
    pattern->maxDelay = 0;
    pattern->accuracy = 0.5f; // Start with neutral accuracy
    pattern->active = true;
    PScale_context.patternCount++;
  }
  
  if (pattern) {
    // Update pattern statistics
    pattern->frequency++;
    
    // Safety check to prevent divide-by-zero
    if (pattern->frequency > 0) {
      pattern->avgDelay = (pattern->avgDelay * (pattern->frequency - 1) + delay) / pattern->frequency;
    }
    
    pattern->minDelay = min(pattern->minDelay, delay);
    pattern->maxDelay = max(pattern->maxDelay, delay);
    pattern->lastSeen = millis();
    
    // Update accuracy based on successful predictions
    // (This would be updated when predictions are validated)
    
    // Rate-limited logging to prevent spam (only log every 50 pattern updates)
    if (pattern->frequency % 50 == 0) {
      Serial.printf("[PSCALE] Learned pattern: %d->%d, delay=%dms, freq=%d, acc=%.2f\n",
                    triggerType, targetType, pattern->avgDelay, pattern->frequency, pattern->accuracy);
    }
  }
}

void PScale_prunePatterns() {
  uint32_t now = millis();
  
  for (uint8_t i = 0; i < PScale_context.patternCount; i++) {
    PredictivePattern* pattern = &PScale_context.patterns[i];
    
    // Remove patterns with low accuracy or old last-seen time
    if (pattern->accuracy < 0.3f || (now - pattern->lastSeen) > 300000) { // 5 minutes
      Serial.printf("[PSCALE] Pruning pattern %d->%d (acc=%.2f, age=%ds)\n",
                    pattern->triggerType, pattern->targetType, pattern->accuracy,
                    (now - pattern->lastSeen) / 1000);
      
      // Move last pattern to this position
      if (i < PScale_context.patternCount - 1) {
        *pattern = PScale_context.patterns[PScale_context.patternCount - 1];
      }
      PScale_context.patternCount--;
      i--; // Re-check this position
    }
  }
}

// ───── Predictive Scaling Logic ─────
void PScale_makePrediction(PerformanceEventType eventType) {
  if (!PScale_context.adaptiveScalingEnabled) return;
  
  uint32_t now = millis();
  
  // Look for patterns that match this trigger event
  for (uint8_t i = 0; i < PScale_context.patternCount; i++) {
    PredictivePattern* pattern = &PScale_context.patterns[i];
    
    if (pattern->triggerType == eventType && pattern->active && 
        pattern->accuracy >= SCALING_PREDICTION_ACCURACY && pattern->frequency >= 3) {
      
      // Calculate when the predicted event should occur
      uint32_t predictedTime = now + pattern->avgDelay;
      
      // Determine required frequency for predicted event
      uint8_t requiredFreq = PScale_getRequiredFreqForEvent(pattern->targetType);
      uint8_t currentFreq = FreqManager_getCurrentFrequency();
      
      if (requiredFreq > currentFreq) {
        // Make proactive scaling decision
        PScale_context.currentPredictedFreq = requiredFreq;
        PScale_context.predictionValidUntil = predictedTime + 2000; // Valid for 2s after predicted time
        
        Serial.printf("[PSCALE] Predictive scaling: %d->%d in %dms (confidence=%.2f)\n",
                      eventType, pattern->targetType, pattern->avgDelay, pattern->accuracy);
        
        FreqManager_setFrequency(requiredFreq);
        PScale_context.totalPredictions++;
        break;
      }
    }
  }
}

uint8_t PScale_getRequiredFreqForEvent(PerformanceEventType eventType) {
  switch (eventType) {
    case PERF_TOUCH_BURST:
    case PERF_UI_TRANSITION:
      return 2; // High frequency for UI responsiveness
      
    case PERF_DISPLAY_INTENSIVE:
      return 2; // High frequency for smooth rendering
      
    case PERF_NETWORK_BURST:
      return 1; // Normal frequency usually sufficient
      
    case PERF_BACKGROUND_SYNC:
      return 0; // Low frequency acceptable
      
    case PERF_SYSTEM_STRESS:
      return 2; // High frequency to handle stress
      
    default:
      return 1; // Default to normal frequency
  }
}

void PScale_validatePrediction(PerformanceEventType actualEvent) {
  uint32_t now = millis();
  
  // Check if we have an active prediction
  if (PScale_context.predictionValidUntil > 0 && now <= PScale_context.predictionValidUntil) {
    // Find the pattern that made this prediction
    for (uint8_t i = 0; i < PScale_context.patternCount; i++) {
      PredictivePattern* pattern = &PScale_context.patterns[i];
      
      if (PScale_getRequiredFreqForEvent(pattern->targetType) == PScale_context.currentPredictedFreq) {
        if (pattern->targetType == actualEvent) {
          // Correct prediction
          pattern->accuracy = min(1.0f, pattern->accuracy + 0.1f);
          PScale_context.correctPredictions++;
          Serial.printf("[PSCALE] Prediction validated: %d (acc=%.2f)\n", actualEvent, pattern->accuracy);
        } else {
          // Incorrect prediction
          pattern->accuracy = max(0.0f, pattern->accuracy - 0.05f);
          Serial.printf("[PSCALE] Prediction failed: expected %d, got %d (acc=%.2f)\n", 
                        pattern->targetType, actualEvent, pattern->accuracy);
        }
        break;
      }
    }
    
    // Clear prediction
    PScale_context.predictionValidUntil = 0;
  }
  
  // Update overall accuracy
  if (PScale_context.totalPredictions > 0) {
    PScale_context.overallAccuracy = (float)PScale_context.correctPredictions / PScale_context.totalPredictions;
  }
}

// ───── System Load Monitoring ─────
void PScale_updateSystemContext() {
  SystemPerformanceContext* ctx = &PScale_context.systemContext;
  uint32_t now = millis();
  
  // Sample system performance
  uint32_t currentLoad = FreqManager_getCPULoad();
  uint32_t memPressure = MemoryManager_getMemoryPressure();
  uint32_t batteryLevel = PowerManager_getBatteryLevel();
  
  // Update load statistics
  ctx->currentLoad = currentLoad;
  ctx->avgLoad = (ctx->avgLoad * 7 + currentLoad) / 8; // Running average - safe since 8 is constant
  ctx->peakLoad = max(ctx->peakLoad, currentLoad);
  ctx->memoryPressure = memPressure;
  
  // Detect overload conditions
  if (currentLoad > 80 || memPressure > 85) {
    ctx->consecutiveOverloads++;
    
    if (ctx->consecutiveOverloads > 3) {
      // System is consistently overloaded - force high frequency
      if (FreqManager_getCurrentFrequency() < 2) {
        Serial.println("[PSCALE] System overload detected - forcing high frequency");
        FreqManager_setFrequency(2);
        PScale_recordEvent(PERF_SYSTEM_STRESS, 1000, 9);
      }
    }
  } else {
    ctx->consecutiveOverloads = 0;
  }
  
  // Update battery drain factor
  if (batteryLevel < 100) {
    static uint32_t lastBatteryCheck = 0;
    static uint32_t lastBatteryLevel = 100;
    
    if (now - lastBatteryCheck > 60000) { // Check every minute
      if (batteryLevel < lastBatteryLevel) {
        ctx->batteryDrainRate = (lastBatteryLevel - batteryLevel) / 60.0f;
      }
      lastBatteryLevel = batteryLevel;
      lastBatteryCheck = now;
    }
  }
  
  // Thermal throttling detection (simplified)
  uint8_t currentFreq = FreqManager_getCurrentFrequency();
  if (currentFreq == 2 && ctx->currentLoad < 50) {
    // High frequency with low load might indicate thermal throttling
    ctx->thermalThrottling = true;
  } else {
    ctx->thermalThrottling = false;
  }
}

// ───── Event Detection and Integration ─────
void PScale_detectAndRecordEvents() {
  // Detect touch bursts
  if (TouchManager_isBurst()) {
    PScale_recordEvent(PERF_TOUCH_BURST, 200, 8);
    PScale_makePrediction(PERF_TOUCH_BURST);
  }
  
  // Detect network bursts
  if (NetworkTask_isBurst()) {
    PScale_recordEvent(PERF_NETWORK_BURST, 500, 6);
    PScale_makePrediction(PERF_NETWORK_BURST);
  }
  
  // Detect intensive display operations
  if (DisplayManager_isIntensiveRender()) {
    PScale_recordEvent(PERF_DISPLAY_INTENSIVE, 300, 7);
    PScale_makePrediction(PERF_DISPLAY_INTENSIVE);
  }
  
  // Detect UI transitions/animations
  if (AnimationManager_isAnyAnimationActive()) {
    PScale_recordEvent(PERF_UI_TRANSITION, 250, 7);
    PScale_makePrediction(PERF_UI_TRANSITION);
  }
}

// ───── Core Functions ─────
void PScale_init() {
  if (!ENABLE_PREDICTIVE_SCALING) {
    Serial.println("[PSCALE] Predictive scaling disabled");
    return;
  }
  
  Serial.println("[PSCALE] Initializing predictive performance scaling...");
  
  // Initialize context
  memset(&PScale_context, 0, sizeof(PredictiveScalingContext));
  PScale_context.adaptiveScalingEnabled = true;
  
  PScale_initialized = true;
  Serial.println("[PSCALE] Predictive scaling initialized");
}

void PScale_update() {
  if (!ENABLE_PREDICTIVE_SCALING || !PScale_initialized) return;
  
  uint32_t now = millis();
  
  // Update system performance context
  if (now - PScale_lastPerformanceSample >= PERFORMANCE_SAMPLE_INTERVAL_MS) {
    PScale_updateSystemContext();
    PScale_lastPerformanceSample = now;
  }
  
  // Detect and record performance events
  PScale_detectAndRecordEvents();
  
  // Analyze patterns periodically
  PScale_analyzePatterns();
  
  // Check if prediction window has expired
  if (PScale_context.predictionValidUntil > 0 && now > PScale_context.predictionValidUntil) {
    PScale_context.predictionValidUntil = 0;
    
    // Consider downscaling if no high-intensity events are predicted
    if (PScale_context.systemContext.currentLoad < 30 && 
        FreqManager_getCurrentFrequency() > 1) {
      Serial.println("[PSCALE] Prediction window expired - considering downscale");
      FreqManager_setFrequency(1);
    }
  }
}

// ───── Control Functions ─────
void PScale_notifyEvent(PerformanceEventType eventType, uint16_t duration, uint8_t intensity) {
  if (ENABLE_PREDICTIVE_SCALING && PScale_initialized) {
    PScale_recordEvent(eventType, duration, intensity);
    PScale_validatePrediction(eventType);
    PScale_makePrediction(eventType);
  }
}

// Overloaded version for uint8_t eventType
void PScale_notifyEvent(uint8_t eventType, uint16_t duration, uint8_t intensity) {
  // Convert uint8_t to PerformanceEventType and call the main function
  PScale_notifyEvent((PerformanceEventType)eventType, duration, intensity);
}

bool PScale_isAdaptiveScalingEnabled() {
  return ENABLE_PREDICTIVE_SCALING && PScale_context.adaptiveScalingEnabled;
}

void PScale_setAdaptiveScaling(bool enabled) {
  PScale_context.adaptiveScalingEnabled = enabled;
  Serial.printf("[PSCALE] Adaptive scaling %s\n", enabled ? "enabled" : "disabled");
}

// ───── Statistics and Reporting ─────
void PScale_printStats() {
  if (!ENABLE_PREDICTIVE_SCALING) {
    Serial.println("[PSCALE] Predictive scaling disabled");
    return;
  }
  
  SystemPerformanceContext* ctx = &PScale_context.systemContext;
  
  Serial.printf("[PSCALE] System load: %d%% (avg=%d%%, peak=%d%%)\n",
                ctx->currentLoad, ctx->avgLoad, ctx->peakLoad);
  Serial.printf("[PSCALE] Memory pressure: %d%%, Battery drain: %.2f%%/min\n",
                ctx->memoryPressure, ctx->batteryDrainRate);
  Serial.printf("[PSCALE] Patterns learned: %d, Overall accuracy: %.2f%%\n",
                PScale_context.patternCount, PScale_context.overallAccuracy * 100.0f);
  Serial.printf("[PSCALE] Predictions: %d total, %d correct\n",
                PScale_context.totalPredictions, PScale_context.correctPredictions);
  
  if (PScale_context.predictionValidUntil > 0) {
    uint32_t remaining = PScale_context.predictionValidUntil - millis();
    Serial.printf("[PSCALE] Active prediction: freq=%d, valid for %dms\n",
                  PScale_context.currentPredictedFreq, remaining);
  }
  
  Serial.println("[PSCALE] Top patterns:");
  for (uint8_t i = 0; i < min((uint8_t)5, PScale_context.patternCount); i++) {
    PredictivePattern* pattern = &PScale_context.patterns[i];
    Serial.printf("  %d->%d: delay=%dms, freq=%d, acc=%.2f\n",
                  pattern->triggerType, pattern->targetType, pattern->avgDelay,
                  pattern->frequency, pattern->accuracy);
  }
}

float PScale_getOverallAccuracy() {
  return PScale_context.overallAccuracy;
}

uint8_t PScale_getPatternCount() {
  return PScale_context.patternCount;
}

uint32_t PScale_getTotalPredictions() {
  return PScale_context.totalPredictions;
}

#else // !ENABLE_PREDICTIVE_SCALING

// ───── Stub Functions When Predictive Scaling is Disabled ─────
void PScale_init() {}
void PScale_update() {}
void PScale_notifyEvent(PerformanceEventType eventType, uint16_t duration, uint8_t intensity) {}
void PScale_notifyEvent(uint8_t eventType, uint16_t duration, uint8_t intensity) {}
bool PScale_isAdaptiveScalingEnabled() { return false; }
void PScale_setAdaptiveScaling(bool enabled) {}
void PScale_printStats() { Serial.println("[PSCALE] Predictive scaling disabled"); }
float PScale_getOverallAccuracy() { return 0.0f; }
uint8_t PScale_getPatternCount() { return 0; }
uint32_t PScale_getTotalPredictions() { return 0; }

#endif // ENABLE_PREDICTIVE_SCALING