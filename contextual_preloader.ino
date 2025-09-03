// ─────────────────────────────────────────────────────────────
// Contextual Asset Preloader - PHASE 4: Intelligent Asset Management
// Learns user navigation patterns and preloads assets for instant page transitions
// ─────────────────────────────────────────────────────────────

#if ENABLE_SMART_CACHING

#include <SD.h>

// ───── Preloading Configuration ─────
#define MAX_NAVIGATION_PATTERNS 24        // Maximum learned navigation patterns
#define MAX_PRELOADED_ASSETS 32          // Maximum assets kept in memory
#define PATTERN_CONFIDENCE_THRESHOLD 0.6f // Minimum confidence for preloading
#define ASSET_CACHE_SIZE 196608          // 192KB asset cache (PSRAM)
#define NAVIGATION_HISTORY_SIZE 48       // Track last 48 page transitions
#define PRELOAD_PREDICTION_WINDOW_MS 2000 // Preload assets within 2s prediction

// ───── Navigation Pattern Structure and Asset Cache Entry ─────
// These are now defined in the main .ino file

// ───── Navigation History Entry ─────
typedef struct {
  int8_t fromPage;
  int8_t toPage;
  uint32_t timestamp;
  uint16_t transitionTime;       // Time taken for transition
  uint8_t interactionIntensity;  // User interaction intensity during transition
} NavigationHistoryEntry;

// ───── Preload Prediction ─────
typedef struct {
  int8_t predictedPage;          // Next predicted page
  uint32_t predictionTime;       // When prediction was made
  uint32_t predictionValidUntil; // When prediction expires
  float confidence;              // Prediction confidence
  bool assetsPreloaded;          // Assets for predicted page are loaded
} PreloadPrediction;

// ───── Contextual Preloader Context ─────
typedef struct {
  NavigationPattern patterns[MAX_NAVIGATION_PATTERNS];
  uint8_t patternCount;
  AssetCacheEntry assetCache[MAX_PRELOADED_ASSETS];
  uint8_t assetCount;
  NavigationHistoryEntry history[NAVIGATION_HISTORY_SIZE];
  uint8_t historyIndex;
  PreloadPrediction currentPrediction;
  uint8_t* cacheMemoryPool;      // PSRAM memory pool for assets
  uint32_t memoryPoolUsed;       // Bytes used in memory pool
  uint32_t totalPatternsLearned; // Statistics
  uint32_t totalPredictions;
  uint32_t correctPredictions;
  uint32_t assetsPreloaded;
  uint32_t cacheHits;
  uint32_t cacheMisses;
  bool intelligentPreloadEnabled;
  bool adaptiveCacheEnabled;
  int8_t currentPage;
  uint32_t lastPageChange;
} ContextualPreloaderContext;

// ───── Global State ─────
static ContextualPreloaderContext CPreload_context = {0};
static bool CPreload_initialized = false;

// Forward declarations
bool SDManager_loadAsset(const char* filename, uint8_t** data, uint32_t* size);
int UIManager_getCurrentPage();
void FreqManager_notifyDisplayActivity();
void MemoryManager_notifyAllocation(uint32_t size);
void MemoryManager_notifyDeallocation(uint32_t size);
uint32_t MemoryManager_getAvailablePSRAM();

// ───── Navigation Pattern Learning ─────
void CPreload_recordNavigation(int8_t fromPage, int8_t toPage, uint16_t transitionTime) {
  if (!CPreload_initialized || fromPage == toPage) return;
  
  // Add to history
  NavigationHistoryEntry* entry = &CPreload_context.history[CPreload_context.historyIndex];
  entry->fromPage = fromPage;
  entry->toPage = toPage;
  entry->timestamp = millis();
  entry->transitionTime = transitionTime;
  entry->interactionIntensity = 5; // Default intensity
  
  CPreload_context.historyIndex = (CPreload_context.historyIndex + 1) % NAVIGATION_HISTORY_SIZE;
  
  // Update or create navigation pattern
  NavigationPattern* pattern = CPreload_findOrCreatePattern(fromPage, toPage);
  if (pattern) {
    pattern->frequency++;
    pattern->totalTransitionTime += transitionTime;
    pattern->avgTransitionTime = pattern->totalTransitionTime / pattern->frequency;
    pattern->lastSeen = millis();
    
    // Calculate confidence based on frequency and consistency
    float consistency = CPreload_calculatePatternConsistency(pattern);
    pattern->confidence = min(1.0f, (pattern->frequency / 10.0f) * consistency);
    pattern->active = pattern->confidence >= PATTERN_CONFIDENCE_THRESHOLD;
    
    // Record time of day for temporal patterns
    time_t rawtime = millis() / 1000;
    struct tm* timeinfo = localtime(&rawtime);
    pattern->timeOfDay = timeinfo->tm_hour;
    
    Serial.printf("[CPRELOAD] Pattern %d->%d: freq=%d, conf=%.2f, active=%s\n",
                  fromPage, toPage, pattern->frequency, pattern->confidence,
                  pattern->active ? "YES" : "NO");
  }
  
  // Make prediction for next page
  CPreload_makePrediction(toPage);
  
  // Update current page
  CPreload_context.currentPage = toPage;
  CPreload_context.lastPageChange = millis();
}

NavigationPattern* CPreload_findOrCreatePattern(int8_t fromPage, int8_t toPage) {
  // Find existing pattern
  for (uint8_t i = 0; i < CPreload_context.patternCount; i++) {
    NavigationPattern* pattern = &CPreload_context.patterns[i];
    if (pattern->fromPage == fromPage && pattern->toPage == toPage) {
      return pattern;
    }
  }
  
  // Create new pattern if space available
  if (CPreload_context.patternCount < MAX_NAVIGATION_PATTERNS) {
    NavigationPattern* pattern = &CPreload_context.patterns[CPreload_context.patternCount];
    pattern->fromPage = fromPage;
    pattern->toPage = toPage;
    pattern->frequency = 0;
    pattern->totalTransitionTime = 0;
    pattern->avgTransitionTime = 0;
    pattern->lastSeen = 0;
    pattern->confidence = 0.0f;
    pattern->active = false;
    pattern->timeOfDay = 0;
    pattern->sequenceLength = 1;
    
    CPreload_context.patternCount++;
    CPreload_context.totalPatternsLearned++;
    
    Serial.printf("[CPRELOAD] Created new pattern: %d->%d (total=%d)\n",
                  fromPage, toPage, CPreload_context.patternCount);
    
    return pattern;
  }
  
  return nullptr;
}

float CPreload_calculatePatternConsistency(NavigationPattern* pattern) {
  if (pattern->frequency < 3) return 0.5f; // Not enough data
  
  // Calculate variance in transition times
  uint32_t avgTime = pattern->avgTransitionTime;
  uint64_t variance = 0;
  uint8_t samples = 0;
  
  // Look through history for this pattern
  for (uint8_t i = 0; i < NAVIGATION_HISTORY_SIZE; i++) {
    NavigationHistoryEntry* entry = &CPreload_context.history[i];
    if (entry->fromPage == pattern->fromPage && entry->toPage == pattern->toPage) {
      int32_t diff = entry->transitionTime - avgTime;
      variance += diff * diff;
      samples++;
    }
  }
  
  if (samples == 0) return 0.5f;
  
  variance /= samples;
  float standardDev = sqrt(variance);
  
  // Lower variance = higher consistency
  float consistency = 1.0f - min(standardDev / avgTime, 1.0f);
  return max(0.1f, consistency);
}

// ───── Prediction Engine ─────
void CPreload_makePrediction(int8_t currentPage) {
  PreloadPrediction* pred = &CPreload_context.currentPrediction;
  
  // Clear existing prediction
  pred->predictedPage = -1;
  pred->predictionTime = millis();
  pred->predictionValidUntil = pred->predictionTime + PRELOAD_PREDICTION_WINDOW_MS;
  pred->confidence = 0.0f;
  pred->assetsPreloaded = false;
  
  // Find highest confidence pattern from current page
  NavigationPattern* bestPattern = nullptr;
  float highestConfidence = 0.0f;
  
  for (uint8_t i = 0; i < CPreload_context.patternCount; i++) {
    NavigationPattern* pattern = &CPreload_context.patterns[i];
    
    if (pattern->fromPage == currentPage && pattern->active) {
      // Consider time-of-day patterns
      time_t rawtime = millis() / 1000;
      struct tm* timeinfo = localtime(&rawtime);
      uint8_t currentHour = timeinfo->tm_hour;
      
      float timeBonus = 1.0f;
      if (abs(currentHour - pattern->timeOfDay) <= 1) {
        timeBonus = 1.2f; // 20% boost for time-of-day match
      }
      
      float adjustedConfidence = pattern->confidence * timeBonus;
      
      if (adjustedConfidence > highestConfidence) {
        highestConfidence = adjustedConfidence;
        bestPattern = pattern;
      }
    }
  }
  
  if (bestPattern && highestConfidence >= PATTERN_CONFIDENCE_THRESHOLD) {
    pred->predictedPage = bestPattern->toPage;
    pred->confidence = highestConfidence;
    
    Serial.printf("[CPRELOAD] Predicted navigation: %d->%d (conf=%.2f)\n",
                  currentPage, pred->predictedPage, pred->confidence);
    
    // Preload assets for predicted page
    CPreload_preloadPageAssets(pred->predictedPage);
    
    CPreload_context.totalPredictions++;
  }
}

void CPreload_validatePrediction(int8_t actualNextPage) {
  PreloadPrediction* pred = &CPreload_context.currentPrediction;
  uint32_t now = millis();
  
  if (now <= pred->predictionValidUntil && pred->predictedPage >= 0) {
    if (pred->predictedPage == actualNextPage) {
      CPreload_context.correctPredictions++;
      Serial.printf("[CPRELOAD] Prediction validated: %d (accuracy=%.2f%%)\n",
                    actualNextPage, CPreload_getPredictionAccuracy() * 100.0f);
    } else {
      Serial.printf("[CPRELOAD] Prediction failed: expected %d, got %d\n",
                    pred->predictedPage, actualNextPage);
    }
  }
}

// ───── Asset Cache Management ─────
bool CPreload_initAssetCache() {
  // Allocate PSRAM memory pool for assets
  CPreload_context.cacheMemoryPool = (uint8_t*)ps_malloc(ASSET_CACHE_SIZE);
  if (!CPreload_context.cacheMemoryPool) {
    Serial.println("[CPRELOAD] Failed to allocate asset cache memory");
    return false;
  }
  
  CPreload_context.memoryPoolUsed = 0;
  Serial.printf("[CPRELOAD] Asset cache initialized: %d bytes\n", ASSET_CACHE_SIZE);
  
  return true;
}

AssetCacheEntry* CPreload_findAssetEntry(const char* filename) {
  for (uint8_t i = 0; i < CPreload_context.assetCount; i++) {
    if (strcmp(CPreload_context.assetCache[i].filename, filename) == 0) {
      return &CPreload_context.assetCache[i];
    }
  }
  return nullptr;
}

bool CPreload_cacheAsset(const char* filename, int8_t associatedPage, float priority) {
  // Check if already cached
  AssetCacheEntry* entry = CPreload_findAssetEntry(filename);
  if (entry) {
    entry->lastAccessed = millis();
    entry->priority = max(entry->priority, priority);
    return true;
  }
  
  // Check if we have space for new entry
  if (CPreload_context.assetCount >= MAX_PRELOADED_ASSETS) {
    // Evict least important asset
    if (!CPreload_evictAsset()) {
      Serial.println("[CPRELOAD] Cannot evict asset - cache full");
      return false;
    }
  }
  
  // Load asset from SD card
  uint8_t* assetData = nullptr;
  uint32_t assetSize = 0;
  
  if (!SDManager_loadAsset(filename, &assetData, &assetSize)) {
    Serial.printf("[CPRELOAD] Failed to load asset: %s\n", filename);
    return false;
  }
  
  // Check if asset fits in memory pool
  if (CPreload_context.memoryPoolUsed + assetSize > ASSET_CACHE_SIZE) {
    // Try to free space by evicting assets
    while (CPreload_context.memoryPoolUsed + assetSize > ASSET_CACHE_SIZE && 
           CPreload_evictAsset()) {
      // Keep evicting until we have space
    }
    
    if (CPreload_context.memoryPoolUsed + assetSize > ASSET_CACHE_SIZE) {
      Serial.printf("[CPRELOAD] Asset too large for cache: %s (%d bytes)\n", 
                    filename, assetSize);
      free(assetData);
      return false;
    }
  }
  
  // Copy asset data to cache memory pool
  uint8_t* cacheLocation = CPreload_context.cacheMemoryPool + CPreload_context.memoryPoolUsed;
  memcpy(cacheLocation, assetData, assetSize);
  free(assetData); // Free the temporary allocation
  
  // Create cache entry
  entry = &CPreload_context.assetCache[CPreload_context.assetCount];
  strncpy(entry->filename, filename, sizeof(entry->filename) - 1);
  entry->filename[sizeof(entry->filename) - 1] = '\0';
  entry->data = cacheLocation;
  entry->size = assetSize;
  entry->associatedPage = associatedPage;
  entry->lastAccessed = millis();
  entry->accessCount = 1;
  entry->priority = priority;
  entry->persistent = (priority >= 0.9f);
  entry->preloaded = true;
  
  CPreload_context.memoryPoolUsed += assetSize;
  CPreload_context.assetCount++;
  CPreload_context.assetsPreloaded++;
  
  Serial.printf("[CPRELOAD] Cached asset: %s (%d bytes, page %d, priority %.2f)\n",
                filename, assetSize, associatedPage, priority);
  
  MemoryManager_notifyAllocation(assetSize);
  
  return true;
}

bool CPreload_evictAsset() {
  if (CPreload_context.assetCount == 0) return false;
  
  // Find least important asset to evict
  AssetCacheEntry* evictCandidate = nullptr;
  float lowestScore = 999.0f;
  uint8_t evictIndex = 0;
  
  uint32_t now = millis();
  
  for (uint8_t i = 0; i < CPreload_context.assetCount; i++) {
    AssetCacheEntry* entry = &CPreload_context.assetCache[i];
    
    if (entry->persistent) continue; // Never evict persistent assets
    
    // Calculate eviction score (lower = more likely to evict)
    float ageScore = (now - entry->lastAccessed) / 60000.0f; // Age in minutes
    float accessScore = 1.0f / max(entry->accessCount, (uint32_t)1);  // Inverse access frequency
    float priorityScore = 1.0f - entry->priority;            // Inverse priority
    
    float evictionScore = ageScore + accessScore + priorityScore;
    
    if (evictionScore < lowestScore) {
      lowestScore = evictionScore;
      evictCandidate = entry;
      evictIndex = i;
    }
  }
  
  if (evictCandidate) {
    Serial.printf("[CPRELOAD] Evicting asset: %s (%d bytes, score=%.2f)\n",
                  evictCandidate->filename, evictCandidate->size, lowestScore);
    
    // Update memory pool usage
    CPreload_context.memoryPoolUsed -= evictCandidate->size;
    
    // Compact cache array
    for (uint8_t i = evictIndex; i < CPreload_context.assetCount - 1; i++) {
      CPreload_context.assetCache[i] = CPreload_context.assetCache[i + 1];
    }
    CPreload_context.assetCount--;
    
    MemoryManager_notifyDeallocation(evictCandidate->size);
    
    return true;
  }
  
  return false;
}

// ───── Page Asset Preloading ─────
void CPreload_preloadPageAssets(int8_t pageNumber) {
  if (!CPreload_context.intelligentPreloadEnabled || pageNumber < 0) return;
  
  Serial.printf("[CPRELOAD] Preloading assets for page %d\n", pageNumber);
  
  // Define assets for each page (this would be configured based on actual UI)
  const char* pageAssets[][4] = {
    {"page0_bg.bmp", "preset_01.bmp", "preset_02.bmp", nullptr},      // Page 0
    {"page1_bg.bmp", "brightness.bmp", "slider.bmp", nullptr},        // Page 1  
    {"page2_bg.bmp", "palette_01.bmp", "palette_02.bmp", nullptr},    // Page 2
    {"page3_bg.bmp", "settings.bmp", "info.bmp", nullptr}             // Page 3
  };
  
  if (pageNumber >= (sizeof(pageAssets) / sizeof(pageAssets[0]))) {
    return; // Page number out of range
  }
  
  // Calculate priority based on prediction confidence
  float basePriority = CPreload_context.currentPrediction.confidence * 0.8f;
  
  // Preload each asset for the page
  for (uint8_t i = 0; i < 4 && pageAssets[pageNumber][i] != nullptr; i++) {
    const char* filename = pageAssets[pageNumber][i];
    float priority = basePriority + (i * 0.05f); // Slightly different priorities
    
    CPreload_cacheAsset(filename, pageNumber, priority);
  }
  
  CPreload_context.currentPrediction.assetsPreloaded = true;
}

uint8_t* CPreload_getAsset(const char* filename, uint32_t* size) {
  AssetCacheEntry* entry = CPreload_findAssetEntry(filename);
  
  if (entry) {
    // Cache hit
    entry->lastAccessed = millis();
    entry->accessCount++;
    
    if (size) *size = entry->size;
    
    CPreload_context.cacheHits++;
    return entry->data;
  } else {
    // Cache miss
    CPreload_context.cacheMisses++;
    
    Serial.printf("[CPRELOAD] Cache miss: %s\n", filename);
    
    // Optionally load and cache asset on demand
    if (CPreload_context.adaptiveCacheEnabled) {
      CPreload_cacheAsset(filename, CPreload_context.currentPage, 0.5f);
      
      // Try again after caching
      entry = CPreload_findAssetEntry(filename);
      if (entry) {
        if (size) *size = entry->size;
        return entry->data;
      }
    }
    
    return nullptr;
  }
}

// ───── Pattern Management ─────
void CPreload_pruneOldPatterns() {
  uint32_t now = millis();
  uint8_t removeCount = 0;
  
  for (uint8_t i = 0; i < CPreload_context.patternCount; i++) {
    NavigationPattern* pattern = &CPreload_context.patterns[i];
    
    // Remove patterns that haven't been seen in 10 minutes and have low confidence
    if ((now - pattern->lastSeen) > 600000 && pattern->confidence < 0.3f) {
      // Move last pattern to this position
      if (i < CPreload_context.patternCount - 1) {
        CPreload_context.patterns[i] = CPreload_context.patterns[CPreload_context.patternCount - 1];
      }
      CPreload_context.patternCount--;
      i--; // Re-check this position
      removeCount++;
    }
  }
  
  if (removeCount > 0) {
    Serial.printf("[CPRELOAD] Pruned %d old patterns\n", removeCount);
  }
}

// ───── Core Functions ─────
void CPreload_init() {
  if (!ENABLE_SMART_CACHING) {
    Serial.println("[CPRELOAD] Contextual preloading disabled");
    return;
  }
  
  Serial.println("[CPRELOAD] Initializing contextual asset preloader...");
  
  // Initialize context
  memset(&CPreload_context, 0, sizeof(ContextualPreloaderContext));
  
  // Initialize asset cache
  if (!CPreload_initAssetCache()) {
    Serial.println("[CPRELOAD] Failed to initialize asset cache");
    return;
  }
  
  // Set configuration
  CPreload_context.intelligentPreloadEnabled = true;
  CPreload_context.adaptiveCacheEnabled = true;
  CPreload_context.currentPage = 0;
  CPreload_context.lastPageChange = millis();
  
  // Initialize prediction
  CPreload_context.currentPrediction.predictedPage = -1;
  CPreload_context.currentPrediction.confidence = 0.0f;
  
  CPreload_initialized = true;
  
  Serial.printf("[CPRELOAD] Initialized: %d pattern slots, %d asset slots, %dKB cache\n",
                MAX_NAVIGATION_PATTERNS, MAX_PRELOADED_ASSETS, ASSET_CACHE_SIZE / 1024);
}

void CPreload_update() {
  if (!ENABLE_SMART_CACHING || !CPreload_initialized) return;
  
  static uint32_t lastMaintenance = 0;
  uint32_t now = millis();
  
  // Periodic maintenance (every 30 seconds)
  if (now - lastMaintenance > 30000) {
    CPreload_pruneOldPatterns();
    CPreload_performCacheMaintenance();
    lastMaintenance = now;
  }
  
  // Check if current prediction has expired
  if (CPreload_context.currentPrediction.predictionValidUntil > 0 && 
      now > CPreload_context.currentPrediction.predictionValidUntil) {
    CPreload_context.currentPrediction.predictedPage = -1;
    CPreload_context.currentPrediction.confidence = 0.0f;
  }
}

void CPreload_performCacheMaintenance() {
  // Update asset priorities based on access patterns
  for (uint8_t i = 0; i < CPreload_context.assetCount; i++) {
    AssetCacheEntry* entry = &CPreload_context.assetCache[i];
    
    // Decay priority over time if not accessed
    uint32_t timeSinceAccess = millis() - entry->lastAccessed;
    if (timeSinceAccess > 120000) { // 2 minutes
      entry->priority *= 0.95f; // 5% decay
    }
    
    // Boost priority for frequently accessed assets
    if (entry->accessCount > 10) {
      entry->priority = min(1.0f, entry->priority * 1.02f); // 2% boost
    }
  }
}

// ───── Public API ─────
void CPreload_notifyPageChange(int8_t fromPage, int8_t toPage) {
  if (CPreload_initialized && fromPage != toPage) {
    uint32_t now = millis();
    uint16_t transitionTime = now - CPreload_context.lastPageChange;
    
    // Validate prediction
    CPreload_validatePrediction(toPage);
    
    // Record navigation
    CPreload_recordNavigation(fromPage, toPage, transitionTime);
    
    FreqManager_notifyDisplayActivity();
  }
}

bool CPreload_isAssetCached(const char* filename) {
  return CPreload_findAssetEntry(filename) != nullptr;
}

void CPreload_preloadAsset(const char* filename, int8_t page, float priority) {
  if (CPreload_initialized) {
    CPreload_cacheAsset(filename, page, priority);
  }
}

// ───── Statistics and Control ─────
void CPreload_printStats() {
  if (!ENABLE_SMART_CACHING) {
    Serial.println("[CPRELOAD] Contextual preloading disabled");
    return;
  }
  
  float hitRate = 0.0f;
  if (CPreload_context.cacheHits + CPreload_context.cacheMisses > 0) {
    hitRate = (float)CPreload_context.cacheHits / 
              (CPreload_context.cacheHits + CPreload_context.cacheMisses);
  }
  
  Serial.printf("[CPRELOAD] Patterns: %d learned, accuracy=%.1f%%\n",
                CPreload_context.patternCount, CPreload_getPredictionAccuracy() * 100.0f);
  Serial.printf("[CPRELOAD] Cache: %d/%d assets, %d/%dKB used, %.1f%% hit rate\n",
                CPreload_context.assetCount, MAX_PRELOADED_ASSETS,
                CPreload_context.memoryPoolUsed / 1024, ASSET_CACHE_SIZE / 1024,
                hitRate * 100.0f);
  Serial.printf("[CPRELOAD] Stats: %d predictions, %d preloaded, %d hits, %d misses\n",
                CPreload_context.totalPredictions, CPreload_context.assetsPreloaded,
                CPreload_context.cacheHits, CPreload_context.cacheMisses);
  
  if (CPreload_context.currentPrediction.predictedPage >= 0) {
    Serial.printf("[CPRELOAD] Current prediction: page %d (conf=%.2f)\n",
                  CPreload_context.currentPrediction.predictedPage,
                  CPreload_context.currentPrediction.confidence);
  }
}

float CPreload_getPredictionAccuracy() {
  if (CPreload_context.totalPredictions == 0) return 0.0f;
  return (float)CPreload_context.correctPredictions / CPreload_context.totalPredictions;
}

float CPreload_getCacheHitRate() {
  uint32_t totalAccesses = CPreload_context.cacheHits + CPreload_context.cacheMisses;
  if (totalAccesses == 0) return 0.0f;
  return (float)CPreload_context.cacheHits / totalAccesses;
}

uint8_t CPreload_getPatternCount() {
  return CPreload_context.patternCount;
}

uint32_t CPreload_getCacheMemoryUsed() {
  return CPreload_context.memoryPoolUsed;
}

void CPreload_setIntelligentPreload(bool enabled) {
  CPreload_context.intelligentPreloadEnabled = enabled;
  Serial.printf("[CPRELOAD] Intelligent preloading %s\n", enabled ? "enabled" : "disabled");
}

bool CPreload_isIntelligentPreloadEnabled() {
  return CPreload_context.intelligentPreloadEnabled;
}

#else // !ENABLE_SMART_CACHING

// ───── Stub Functions When Contextual Preloading is Disabled ─────
void CPreload_init() {}
void CPreload_update() {}
void CPreload_notifyPageChange(int8_t fromPage, int8_t toPage) {}
bool CPreload_isAssetCached(const char* filename) { return false; }
void CPreload_preloadAsset(const char* filename, int8_t page, float priority) {}
uint8_t* CPreload_getAsset(const char* filename, uint32_t* size) { return nullptr; }
void CPreload_printStats() { Serial.println("[CPRELOAD] Contextual preloading disabled"); }
float CPreload_getPredictionAccuracy() { return 0.0f; }
float CPreload_getCacheHitRate() { return 0.0f; }
uint8_t CPreload_getPatternCount() { return 0; }
uint32_t CPreload_getCacheMemoryUsed() { return 0; }
void CPreload_setIntelligentPreload(bool enabled) {}
bool CPreload_isIntelligentPreloadEnabled() { return false; }

#endif // ENABLE_SMART_CACHING