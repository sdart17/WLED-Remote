// ─────────────────────────────────────────────────────────────
// Performance Profiler - PHASE 3: Real-Time System Performance Analysis
// Provides detailed timing analysis, bottleneck detection, and performance metrics
// ─────────────────────────────────────────────────────────────

// ───── Profiler Configuration ─────
#define ENABLE_PERFORMANCE_PROFILER true  // Enable detailed performance profiling
#define MAX_PROFILE_POINTS 16             // Maximum number of profiling points
#define PROFILE_HISTORY_SIZE 64           // Keep history of last 64 measurements
#define PROFILE_REPORT_INTERVAL_MS 60000  // Report every 60 seconds

#if ENABLE_PERFORMANCE_PROFILER

// ───── Profiling Data Structures ─────
struct ProfilePoint {
  char name[32];
  uint32_t totalTime;
  uint32_t callCount;
  uint32_t maxTime;
  uint32_t minTime;
  uint32_t lastTime;
  uint32_t startTime;
  bool active;
  uint32_t history[PROFILE_HISTORY_SIZE];
  uint8_t historyIndex;
};

struct SystemMetrics {
  uint32_t loopCount;
  uint32_t maxLoopTime;
  uint32_t avgLoopTime;
  uint32_t totalLoopTime;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t maxHeapUsage;
  uint8_t cpuFrequency;
  uint32_t uptime;
};

// ───── Global Profiler State ─────
static ProfilePoint Profiler_points[MAX_PROFILE_POINTS];
static uint8_t Profiler_pointCount = 0;
static SystemMetrics Profiler_systemMetrics = {0};
static uint32_t Profiler_lastReport = 0;
static bool Profiler_initialized = false;

// ───── Core Profiling Functions ─────

uint8_t Profiler_createPoint(const char* name) {
  if (Profiler_pointCount >= MAX_PROFILE_POINTS) {
    Serial.printf("[PROFILER] Max profile points reached, cannot create: %s\n", name);
    return 255; // Invalid index
  }
  
  uint8_t index = Profiler_pointCount++;
  ProfilePoint* point = &Profiler_points[index];
  
  strncpy(point->name, name, sizeof(point->name) - 1);
  point->name[sizeof(point->name) - 1] = '\0';
  point->totalTime = 0;
  point->callCount = 0;
  point->maxTime = 0;
  point->minTime = UINT32_MAX;
  point->lastTime = 0;
  point->startTime = 0;
  point->active = false;
  point->historyIndex = 0;
  memset(point->history, 0, sizeof(point->history));
  
  Serial.printf("[PROFILER] Created profile point: %s (index %d)\n", name, index);
  return index;
}

void Profiler_startTiming(uint8_t pointIndex) {
  if (pointIndex >= Profiler_pointCount) return;
  
  ProfilePoint* point = &Profiler_points[pointIndex];
  if (point->active) {
    Serial.printf("[PROFILER] Warning: %s already active\n", point->name);
    return;
  }
  
  point->startTime = micros();
  point->active = true;
}

void Profiler_endTiming(uint8_t pointIndex) {
  uint32_t endTime = micros();
  
  if (pointIndex >= Profiler_pointCount) return;
  
  ProfilePoint* point = &Profiler_points[pointIndex];
  if (!point->active) {
    Serial.printf("[PROFILER] Warning: %s not active\n", point->name);
    return;
  }
  
  uint32_t duration = endTime - point->startTime;
  
  // Update statistics
  point->totalTime += duration;
  point->callCount++;
  point->lastTime = duration;
  
  if (duration > point->maxTime) {
    point->maxTime = duration;
  }
  if (duration < point->minTime) {
    point->minTime = duration;
  }
  
  // Add to history
  point->history[point->historyIndex] = duration;
  point->historyIndex = (point->historyIndex + 1) % PROFILE_HISTORY_SIZE;
  
  point->active = false;
}

// ───── Convenience Macros ─────
#define PROFILER_START(name) \
  static uint8_t profile_##name = 255; \
  if (profile_##name == 255) profile_##name = Profiler_createPoint(#name); \
  Profiler_startTiming(profile_##name);

#define PROFILER_END(name) \
  Profiler_endTiming(profile_##name);

// ───── System Metrics Collection ─────
void Profiler_updateSystemMetrics() {
  static uint32_t lastLoopTime = 0;
  uint32_t now = millis();
  
  // Update loop timing
  uint32_t loopTime = now - lastLoopTime;
  if (lastLoopTime > 0) { // Skip first measurement
    Profiler_systemMetrics.loopCount++;
    Profiler_systemMetrics.totalLoopTime += loopTime;
    
    if (loopTime > Profiler_systemMetrics.maxLoopTime) {
      Profiler_systemMetrics.maxLoopTime = loopTime;
    }
    
    // Calculate running average
    Profiler_systemMetrics.avgLoopTime = 
      Profiler_systemMetrics.totalLoopTime / Profiler_systemMetrics.loopCount;
  }
  lastLoopTime = now;
  
  // Update memory metrics
  uint32_t currentHeap = ESP.getFreeHeap();
  Profiler_systemMetrics.freeHeap = currentHeap;
  
  if (currentHeap < Profiler_systemMetrics.minFreeHeap || Profiler_systemMetrics.minFreeHeap == 0) {
    Profiler_systemMetrics.minFreeHeap = currentHeap;
  }
  
  uint32_t heapUsage = ESP.getHeapSize() - currentHeap;
  if (heapUsage > Profiler_systemMetrics.maxHeapUsage) {
    Profiler_systemMetrics.maxHeapUsage = heapUsage;
  }
  
  // Update CPU frequency
  Profiler_systemMetrics.cpuFrequency = getCpuFrequencyMhz();
  
  // Update uptime
  Profiler_systemMetrics.uptime = now;
}

// ───── Performance Analysis ─────
uint32_t Profiler_getAverageTime(uint8_t pointIndex) {
  if (pointIndex >= Profiler_pointCount) return 0;
  
  ProfilePoint* point = &Profiler_points[pointIndex];
  return point->callCount > 0 ? point->totalTime / point->callCount : 0;
}

uint32_t Profiler_getRecentAverage(uint8_t pointIndex, uint8_t samples) {
  if (pointIndex >= Profiler_pointCount || samples == 0) return 0;
  if (samples > PROFILE_HISTORY_SIZE) samples = PROFILE_HISTORY_SIZE;
  
  ProfilePoint* point = &Profiler_points[pointIndex];
  uint32_t sum = 0;
  uint8_t count = 0;
  
  // Calculate average of last N samples
  for (uint8_t i = 0; i < samples; i++) {
    uint8_t histIdx = (point->historyIndex + PROFILE_HISTORY_SIZE - 1 - i) % PROFILE_HISTORY_SIZE;
    if (point->history[histIdx] > 0) {
      sum += point->history[histIdx];
      count++;
    }
  }
  
  return count > 0 ? sum / count : 0;
}

float Profiler_getPerformanceScore(uint8_t pointIndex) {
  if (pointIndex >= Profiler_pointCount) return 0.0f;
  
  ProfilePoint* point = &Profiler_points[pointIndex];
  if (point->callCount == 0) return 100.0f;
  
  uint32_t avgTime = Profiler_getAverageTime(pointIndex);
  uint32_t recentAvg = Profiler_getRecentAverage(pointIndex, 10);
  
  // Score based on consistency (lower variance = higher score)
  float variance = 0.0f;
  if (recentAvg > 0) {
    variance = abs((int32_t)avgTime - (int32_t)recentAvg) / (float)recentAvg;
  }
  
  float score = 100.0f * (1.0f - min(variance, 1.0f));
  return score;
}

// ───── Bottleneck Detection ─────
uint8_t Profiler_findBottleneck() {
  uint8_t bottleneckIndex = 255;
  uint32_t maxAvgTime = 0;
  
  for (uint8_t i = 0; i < Profiler_pointCount; i++) {
    uint32_t avgTime = Profiler_getAverageTime(i);
    if (avgTime > maxAvgTime) {
      maxAvgTime = avgTime;
      bottleneckIndex = i;
    }
  }
  
  return bottleneckIndex;
}

void Profiler_detectAnomalies() {
  for (uint8_t i = 0; i < Profiler_pointCount; i++) {
    ProfilePoint* point = &Profiler_points[i];
    if (point->callCount < 10) continue; // Need enough samples
    
    uint32_t avgTime = Profiler_getAverageTime(i);
    uint32_t recentAvg = Profiler_getRecentAverage(i, 5);
    
    // Check for performance degradation (recent average much higher)
    if (recentAvg > avgTime * 2 && avgTime > 0) {
      Serial.printf("[PROFILER] ANOMALY: %s performance degraded (avg=%dμs, recent=%dμs)\n",
                    point->name, avgTime, recentAvg);
    }
    
    // Check for extremely long execution times
    if (point->lastTime > avgTime * 5 && avgTime > 1000) { // Only for functions > 1ms average
      Serial.printf("[PROFILER] SPIKE: %s took %dμs (avg=%dμs)\n",
                    point->name, point->lastTime, avgTime);
    }
  }
}

// ───── Reporting Functions ─────
void Profiler_printDetailedReport() {
  Serial.println("\n═══════════════════════════════════════");
  Serial.println("         PERFORMANCE REPORT");
  Serial.println("═══════════════════════════════════════");
  
  // System metrics
  Serial.printf("System Uptime: %ds\n", Profiler_systemMetrics.uptime / 1000);
  Serial.printf("CPU Frequency: %dMHz\n", Profiler_systemMetrics.cpuFrequency);
  Serial.printf("Loop Performance: %d loops, avg=%dms, max=%dms\n",
                Profiler_systemMetrics.loopCount,
                Profiler_systemMetrics.avgLoopTime,
                Profiler_systemMetrics.maxLoopTime);
  Serial.printf("Memory: %d free, %d min, %d max used\n",
                Profiler_systemMetrics.freeHeap,
                Profiler_systemMetrics.minFreeHeap,
                Profiler_systemMetrics.maxHeapUsage);
  
  Serial.println("\n─── FUNCTION TIMING ───");
  Serial.printf("%-20s %8s %8s %8s %8s %6s %5s\n",
                "Function", "Calls", "Total", "Avg", "Max", "Last", "Score");
  
  for (uint8_t i = 0; i < Profiler_pointCount; i++) {
    ProfilePoint* point = &Profiler_points[i];
    if (point->callCount == 0) continue;
    
    uint32_t avgTime = Profiler_getAverageTime(i);
    float score = Profiler_getPerformanceScore(i);
    
    Serial.printf("%-20s %8d %8d %8d %8d %6d %5.1f\n",
                  point->name,
                  point->callCount,
                  point->totalTime / 1000, // Convert to ms
                  avgTime,
                  point->maxTime,
                  point->lastTime,
                  score);
  }
  
  // Identify bottleneck
  uint8_t bottleneck = Profiler_findBottleneck();
  if (bottleneck != 255) {
    Serial.printf("\nBottleneck: %s (%dμs avg)\n",
                  Profiler_points[bottleneck].name,
                  Profiler_getAverageTime(bottleneck));
  }
  
  Serial.println("═══════════════════════════════════════\n");
}

void Profiler_printSummary() {
  Serial.printf("[PROFILER] Summary: %d points, bottleneck=%s, heap=%d/%d, freq=%dMHz\n",
                Profiler_pointCount,
                Profiler_pointCount > 0 ? Profiler_points[Profiler_findBottleneck()].name : "none",
                Profiler_systemMetrics.freeHeap / 1000,
                Profiler_systemMetrics.minFreeHeap / 1000,
                Profiler_systemMetrics.cpuFrequency);
}

// ───── Main Profiler Functions ─────
void Profiler_init() {
  if (!ENABLE_PERFORMANCE_PROFILER) {
    Serial.println("[PROFILER] Performance profiling disabled");
    return;
  }
  
  Serial.println("[PROFILER] Initializing performance profiler...");
  
  // Reset all data
  memset(Profiler_points, 0, sizeof(Profiler_points));
  memset(&Profiler_systemMetrics, 0, sizeof(Profiler_systemMetrics));
  Profiler_pointCount = 0;
  Profiler_lastReport = millis();
  Profiler_initialized = true;
  
  Serial.printf("[PROFILER] Initialized with %d profile points, %d history size\n",
                MAX_PROFILE_POINTS, PROFILE_HISTORY_SIZE);
}

void Profiler_update() {
  if (!ENABLE_PERFORMANCE_PROFILER || !Profiler_initialized) return;
  
  // Update system metrics
  Profiler_updateSystemMetrics();
  
  // Detect performance anomalies
  static uint32_t lastAnomalyCheck = 0;
  if (millis() - lastAnomalyCheck > 10000) { // Check every 10 seconds
    Profiler_detectAnomalies();
    lastAnomalyCheck = millis();
  }
  
  // Periodic detailed report
  uint32_t now = millis();
  if (now - Profiler_lastReport > PROFILE_REPORT_INTERVAL_MS) {
    Profiler_printDetailedReport();
    Profiler_lastReport = now;
  }
}

// ───── External Interface ─────
void Profiler_reset() {
  if (!ENABLE_PERFORMANCE_PROFILER) return;
  
  for (uint8_t i = 0; i < Profiler_pointCount; i++) {
    ProfilePoint* point = &Profiler_points[i];
    point->totalTime = 0;
    point->callCount = 0;
    point->maxTime = 0;
    point->minTime = UINT32_MAX;
    memset(point->history, 0, sizeof(point->history));
  }
  
  memset(&Profiler_systemMetrics, 0, sizeof(Profiler_systemMetrics));
  Serial.println("[PROFILER] Reset all profiling data");
}

bool Profiler_isEnabled() {
  return ENABLE_PERFORMANCE_PROFILER && Profiler_initialized;
}

uint8_t Profiler_getPointCount() {
  return Profiler_pointCount;
}

const char* Profiler_getPointName(uint8_t pointIndex) {
  if (pointIndex >= Profiler_pointCount) return nullptr;
  return Profiler_points[pointIndex].name;
}

#else // !ENABLE_PERFORMANCE_PROFILER

// ───── Stub Functions When Profiler is Disabled ─────
uint8_t Profiler_createPoint(const char* name) { return 255; }
void Profiler_startTiming(uint8_t pointIndex) {}
void Profiler_endTiming(uint8_t pointIndex) {}
void Profiler_init() {}
void Profiler_update() {}
void Profiler_printDetailedReport() { Serial.println("[PROFILER] Profiling disabled"); }
void Profiler_printSummary() {}
void Profiler_reset() {}
bool Profiler_isEnabled() { return false; }
uint8_t Profiler_getPointCount() { return 0; }
const char* Profiler_getPointName(uint8_t pointIndex) { return nullptr; }

#define PROFILER_START(name)
#define PROFILER_END(name)

#endif // ENABLE_PERFORMANCE_PROFILER