// ─────────────────────────────────────────────────────────────
// Memory Manager - PHASE 3: Advanced Memory Allocation Optimization
// Implements memory pools, PSRAM utilization, and smart garbage collection
// ─────────────────────────────────────────────────────────────

#if ENABLE_MEMORY_OPTIMIZATION

// ───── Memory Pool Structure ─────
struct MemoryBlock {
  void* ptr;
  size_t size;
  bool inUse;
  uint32_t allocTime;
  uint32_t lastUsed;
};

// ───── Memory Manager State ─────
static uint8_t* MemoryManager_pool = nullptr;
static MemoryBlock MemoryManager_blocks[32]; // Track up to 32 pool allocations
static uint8_t MemoryManager_blockCount = 0;
static size_t MemoryManager_poolOffset = 0;
static bool MemoryManager_poolInitialized = false;

// ───── PSRAM Management ─────
static bool MemoryManager_psramAvailable = false;
static void* MemoryManager_psramCache = nullptr;
static size_t MemoryManager_psramUsed = 0;

// ───── Statistics ─────
static uint32_t MemoryManager_totalAllocations = 0;
static uint32_t MemoryManager_poolHits = 0;
static uint32_t MemoryManager_poolMisses = 0;
static uint32_t MemoryManager_psramAllocations = 0;
static uint32_t MemoryManager_fragmentationEvents = 0;
static uint32_t MemoryManager_lastGarbageCollection = 0;

// ───── Memory Pool Management ─────

bool MemoryManager_initPool() {
  if (MemoryManager_poolInitialized) return true;
  
  Serial.printf("[MEMORY] Initializing %dKB memory pool...\n", MEMORY_POOL_SIZE / 1024);
  
  // Try PSRAM first, then regular heap
  MemoryManager_pool = (uint8_t*)ps_malloc(MEMORY_POOL_SIZE);
  if (!MemoryManager_pool) {
    Serial.println("[MEMORY] PSRAM allocation failed, trying heap...");
    MemoryManager_pool = (uint8_t*)malloc(MEMORY_POOL_SIZE);
  }
  
  if (!MemoryManager_pool) {
    Serial.println("[MEMORY] Failed to allocate memory pool");
    return false;
  }
  
  // Initialize block tracking
  memset(MemoryManager_blocks, 0, sizeof(MemoryManager_blocks));
  MemoryManager_blockCount = 0;
  MemoryManager_poolOffset = 0;
  MemoryManager_poolInitialized = true;
  
  Serial.printf("[MEMORY] Memory pool initialized at %p\n", MemoryManager_pool);
  return true;
}

void* MemoryManager_allocFromPool(size_t size) {
  if (!MemoryManager_poolInitialized) return nullptr;
  
  // Align to 4-byte boundary
  size = (size + 3) & ~3;
  
  // Check if we have space in the pool
  if (MemoryManager_poolOffset + size > MEMORY_POOL_SIZE) {
    MemoryManager_poolMisses++;
    return nullptr; // Pool exhausted
  }
  
  // Find free block slot
  uint8_t blockIndex = 0;
  for (; blockIndex < 32; blockIndex++) {
    if (!MemoryManager_blocks[blockIndex].inUse) break;
  }
  
  if (blockIndex >= 32) {
    MemoryManager_poolMisses++;
    return nullptr; // No block slots available
  }
  
  // Allocate from pool
  void* ptr = MemoryManager_pool + MemoryManager_poolOffset;
  MemoryManager_poolOffset += size;
  
  // Track the allocation
  MemoryManager_blocks[blockIndex].ptr = ptr;
  MemoryManager_blocks[blockIndex].size = size;
  MemoryManager_blocks[blockIndex].inUse = true;
  MemoryManager_blocks[blockIndex].allocTime = millis();
  MemoryManager_blocks[blockIndex].lastUsed = millis();
  
  if (blockIndex >= MemoryManager_blockCount) {
    MemoryManager_blockCount = blockIndex + 1;
  }
  
  MemoryManager_poolHits++;
  MemoryManager_totalAllocations++;
  
  return ptr;
}

bool MemoryManager_freeFromPool(void* ptr) {
  if (!ptr || !MemoryManager_poolInitialized) return false;
  
  // Find the block
  for (uint8_t i = 0; i < MemoryManager_blockCount; i++) {
    if (MemoryManager_blocks[i].inUse && MemoryManager_blocks[i].ptr == ptr) {
      MemoryManager_blocks[i].inUse = false;
      MemoryManager_blocks[i].ptr = nullptr;
      // Keep size and timestamps for defragmentation analysis
      return true;
    }
  }
  
  return false; // Block not found in pool
}

// ───── PSRAM Management ─────

bool MemoryManager_initPSRAM() {
  // Check if PSRAM is available
  size_t psramSize = ESP.getPsramSize();
  if (psramSize == 0) {
    Serial.println("[MEMORY] PSRAM not available");
    return false;
  }
  
  Serial.printf("[MEMORY] PSRAM detected: %dKB total\n", psramSize / 1024);
  
  // Allocate PSRAM cache
  MemoryManager_psramCache = ps_malloc(PSRAM_CACHE_SIZE);
  if (!MemoryManager_psramCache) {
    Serial.println("[MEMORY] Failed to allocate PSRAM cache");
    return false;
  }
  
  MemoryManager_psramAvailable = true;
  MemoryManager_psramUsed = 0;
  
  Serial.printf("[MEMORY] PSRAM cache initialized: %dKB at %p\n", 
                PSRAM_CACHE_SIZE / 1024, MemoryManager_psramCache);
  return true;
}

void* MemoryManager_allocPSRAM(size_t size) {
  if (!MemoryManager_psramAvailable) return nullptr;
  if (MemoryManager_psramUsed + size > PSRAM_CACHE_SIZE) return nullptr;
  
  void* ptr = ps_malloc(size);
  if (ptr) {
    MemoryManager_psramUsed += size;
    MemoryManager_psramAllocations++;
    MemoryManager_totalAllocations++;
  }
  
  return ptr;
}

void MemoryManager_freePSRAM(void* ptr, size_t size) {
  if (ptr) {
    free(ptr);
    if (MemoryManager_psramUsed >= size) {
      MemoryManager_psramUsed -= size;
    }
  }
}

// ───── Smart Memory Allocation ─────

void* MemoryManager_smartAlloc(size_t size) {
  // Strategy: Try pool first for small allocations, PSRAM for large ones
  void* ptr = nullptr;
  
  if (size <= 1024) { // Small allocations use pool
    ptr = MemoryManager_allocFromPool(size);
    if (ptr) return ptr;
    
    // Pool failed, try regular heap
    ptr = malloc(size);
  } else { // Large allocations prefer PSRAM
    ptr = MemoryManager_allocPSRAM(size);
    if (!ptr) {
      ptr = malloc(size); // Fallback to regular heap
    }
  }
  
  if (ptr) {
    MemoryManager_totalAllocations++;
  }
  
  return ptr;
}

void MemoryManager_smartFree(void* ptr, size_t size) {
  if (!ptr) return;
  
  // Try to free from pool first
  if (MemoryManager_freeFromPool(ptr)) {
    return; // Successfully freed from pool
  }
  
  // Check if it's a PSRAM allocation (heuristic)
  if (MemoryManager_psramAvailable && size > 1024) {
    MemoryManager_freePSRAM(ptr, size);
    return;
  }
  
  // Default to regular free
  free(ptr);
}

// ───── Memory Defragmentation ─────

float MemoryManager_calculateFragmentation() {
  if (!MemoryManager_poolInitialized) return 0.0f;
  
  size_t usedSpace = 0;
  size_t freeBlocks = 0;
  
  for (uint8_t i = 0; i < MemoryManager_blockCount; i++) {
    if (MemoryManager_blocks[i].inUse) {
      usedSpace += MemoryManager_blocks[i].size;
    } else if (MemoryManager_blocks[i].size > 0) {
      freeBlocks++;
    }
  }
  
  if (usedSpace == 0) return 0.0f;
  
  float fragmentation = (float)freeBlocks / (float)MemoryManager_blockCount;
  return fragmentation;
}

bool MemoryManager_defragmentPool() {
  if (!MemoryManager_poolInitialized) return false;
  
  float fragmentation = MemoryManager_calculateFragmentation();
  if (fragmentation < MEMORY_DEFRAG_THRESHOLD) {
    return false; // No need to defragment
  }
  
  Serial.printf("[MEMORY] Defragmenting pool (fragmentation: %.1f%%)\n", fragmentation * 100);
  
  // Simple defragmentation: compact active allocations
  uint8_t writeIndex = 0;
  size_t newOffset = 0;
  
  for (uint8_t readIndex = 0; readIndex < MemoryManager_blockCount; readIndex++) {
    if (MemoryManager_blocks[readIndex].inUse) {
      if (writeIndex != readIndex) {
        // Move block metadata
        MemoryManager_blocks[writeIndex] = MemoryManager_blocks[readIndex];
        
        // Update pointer to new location
        void* newPtr = MemoryManager_pool + newOffset;
        if (MemoryManager_blocks[writeIndex].ptr != newPtr) {
          memmove(newPtr, MemoryManager_blocks[writeIndex].ptr, MemoryManager_blocks[writeIndex].size);
          MemoryManager_blocks[writeIndex].ptr = newPtr;
        }
      }
      
      newOffset += MemoryManager_blocks[writeIndex].size;
      writeIndex++;
    }
  }
  
  // Clear unused block entries
  for (uint8_t i = writeIndex; i < MemoryManager_blockCount; i++) {
    memset(&MemoryManager_blocks[i], 0, sizeof(MemoryBlock));
  }
  
  MemoryManager_blockCount = writeIndex;
  MemoryManager_poolOffset = newOffset;
  MemoryManager_fragmentationEvents++;
  
  Serial.printf("[MEMORY] Defragmentation complete: %d active blocks, %d bytes used\n", 
                writeIndex, newOffset);
  return true;
}

// ───── Garbage Collection ─────

void MemoryManager_garbageCollect() {
  uint32_t now = millis();
  uint32_t collected = 0;
  
  // Look for old, unused allocations in pool
  for (uint8_t i = 0; i < MemoryManager_blockCount; i++) {
    if (MemoryManager_blocks[i].inUse) {
      // Mark as recently used if accessed within last 30 seconds
      if (now - MemoryManager_blocks[i].lastUsed > 30000) {
        // Could implement more sophisticated garbage collection here
        // For now, just update statistics
      }
    }
  }
  
  // Attempt defragmentation
  MemoryManager_defragmentPool();
  
  MemoryManager_lastGarbageCollection = now;
  Serial.printf("[MEMORY] Garbage collection completed, %d bytes reclaimed\n", collected);
}

// ───── Main Memory Manager Functions ─────

void MemoryManager_init() {
  if (!ENABLE_MEMORY_OPTIMIZATION) {
    Serial.println("[MEMORY] Advanced memory optimization disabled");
    return;
  }
  
  Serial.println("[MEMORY] Initializing advanced memory management...");
  
  // Initialize memory pool
  if (MemoryManager_initPool()) {
    Serial.println("[MEMORY] Memory pool ready");
  } else {
    Serial.println("[MEMORY] Memory pool initialization failed");
  }
  
  // Initialize PSRAM if available
  if (MemoryManager_initPSRAM()) {
    Serial.println("[MEMORY] PSRAM cache ready");
  }
  
  MemoryManager_lastGarbageCollection = millis();
  
  Serial.println("[MEMORY] Advanced memory management initialized");
  MemoryManager_printStats();
}

void MemoryManager_update() {
  if (!ENABLE_MEMORY_OPTIMIZATION) return;
  
  uint32_t now = millis();
  
  // Periodic garbage collection
  if (now - MemoryManager_lastGarbageCollection > MEMORY_GC_INTERVAL_MS) {
    MemoryManager_garbageCollect();
  }
  
  // Check for critical memory conditions
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < HEAP_WARNING_THRESHOLD) {
    Serial.printf("[MEMORY] Critical memory warning: %d bytes free\n", freeHeap);
    MemoryManager_garbageCollect(); // Force garbage collection
  }
}

// ───── Statistics and Monitoring ─────

void MemoryManager_printStats() {
  Serial.printf("[MEMORY] Stats: Pool hits=%d misses=%d (%.1f%% hit rate)\n",
                MemoryManager_poolHits, MemoryManager_poolMisses,
                MemoryManager_totalAllocations > 0 ? 
                (MemoryManager_poolHits * 100.0f) / MemoryManager_totalAllocations : 0.0f);
  
  if (MemoryManager_poolInitialized) {
    Serial.printf("[MEMORY] Pool: %d/%d bytes used, %d active blocks, %.1f%% fragmented\n",
                  MemoryManager_poolOffset, MEMORY_POOL_SIZE,
                  MemoryManager_blockCount, MemoryManager_calculateFragmentation() * 100);
  }
  
  if (MemoryManager_psramAvailable) {
    Serial.printf("[MEMORY] PSRAM: %d/%d bytes used, %d allocations\n",
                  MemoryManager_psramUsed, PSRAM_CACHE_SIZE, MemoryManager_psramAllocations);
  }
  
  Serial.printf("[MEMORY] System: %d bytes free heap, %d total allocations, %d defrag events\n",
                ESP.getFreeHeap(), MemoryManager_totalAllocations, MemoryManager_fragmentationEvents);
}

size_t MemoryManager_getPoolUsage() {
  return MemoryManager_poolInitialized ? MemoryManager_poolOffset : 0;
}

float MemoryManager_getPoolEfficiency() {
  return MemoryManager_totalAllocations > 0 ? 
         (MemoryManager_poolHits * 100.0f) / MemoryManager_totalAllocations : 0.0f;
}

bool MemoryManager_isPSRAMAvailable() {
  return MemoryManager_psramAvailable;
}

uint32_t MemoryManager_getTotalAllocations() {
  return MemoryManager_totalAllocations;
}

// ───── Memory Health Monitoring ─────

bool MemoryManager_isHealthy() {
  size_t freeHeap = ESP.getFreeHeap();
  float fragmentation = MemoryManager_calculateFragmentation();
  
  return (freeHeap > HEAP_WARNING_THRESHOLD) && (fragmentation < MEMORY_DEFRAG_THRESHOLD);
}

void MemoryManager_forceGarbageCollection() {
  Serial.println("[MEMORY] Forcing garbage collection...");
  MemoryManager_garbageCollect();
}

uint32_t MemoryManager_getMemoryPressure() {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t usedHeap = totalHeap - freeHeap;
  
  // Return pressure as percentage (0-100)
  return (usedHeap * 100) / totalHeap;
}

#else // !ENABLE_MEMORY_OPTIMIZATION

// ───── Stub Functions When Memory Optimization is Disabled ─────

void MemoryManager_init() {}
void MemoryManager_update() {}
void* MemoryManager_smartAlloc(size_t size) { return malloc(size); }
void MemoryManager_smartFree(void* ptr, size_t size) { free(ptr); }
void MemoryManager_printStats() { Serial.println("[MEMORY] Advanced memory optimization disabled"); }
size_t MemoryManager_getPoolUsage() { return 0; }
float MemoryManager_getPoolEfficiency() { return 0.0f; }
bool MemoryManager_isPSRAMAvailable() { return ESP.getPsramSize() > 0; }
uint32_t MemoryManager_getTotalAllocations() { return 0; }
bool MemoryManager_isHealthy() { return ESP.getFreeHeap() > HEAP_WARNING_THRESHOLD; }
void MemoryManager_forceGarbageCollection() {}

uint32_t MemoryManager_getMemoryPressure() {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t usedHeap = totalHeap - freeHeap;
  
  // Return pressure as percentage (0-100)
  return (usedHeap * 100) / totalHeap;
}

#endif // ENABLE_MEMORY_OPTIMIZATION