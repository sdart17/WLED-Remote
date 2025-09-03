// ─────────────────────────────────────────────────────────────
// SD Card and Icon Cache Management - OPTIMIZED (bulk rendering, heap monitoring)
// ─────────────────────────────────────────────────────────────

struct IconCache {
  char path[50];
  uint16_t* pixels;
  bool loaded;
  int16_t w, h;
  uint32_t lastUsed; // OPTIMIZED: LRU tracking
  size_t memorySize; // OPTIMIZED: Memory usage tracking
};

// SD manager state variables
static bool SDManager_sdAvailable = false;
static bool SDManager_assetsPreloaded = false; // PHASE 2A: Track preload state
static SPIClass SDManager_sdSPI(FSPI);
static IconCache SDManager_iconCache[MAX_ICON_CACHE];
static int SDManager_cacheCount = 0;

// PHASE 2A: Increased memory limits for complete icon preloading
static size_t SDManager_totalCacheMemory = 0;
static const size_t MAX_CACHE_MEMORY = 350000; // PHASE 2A: Increased from 100KB to 350KB for all icons

// OPTIMIZED: Performance metrics
static uint32_t SDManager_cacheHits = 0;
static uint32_t SDManager_cacheMisses = 0;

// PHASE 2A: List of all icons to preload at boot
static const char* PRELOAD_ICONS[] = {
  "/370x140-brightness-bw.bmp",
  "/shuffle-80x80-wb-border.bmp", "/shuffle-80x80-bw-border.bmp",
  "/bright-80x80-wb-border.bmp",  "/bright-80x80-bw-border.bmp",
  "/music-80x80-wb-border.bmp",   "/music-80x80-bw-border.bmp",
  "/one-80x80-wb-border.bmp",     "/one-80x80-bw-border.bmp",
  "/two-80x80-wb-border.bmp",     "/two-80x80-bw-border.bmp",
  "/three-80x80-wb-border.bmp",   "/three-80x80-bw-border.bmp",
  "/power-80x80-wb-border.bmp",   "/power-80x80-bw-border.bmp",
  "/up-80x80-wb-border.bmp",      "/up-80x80-bw-border.bmp",
  "/down-80x80-wb-border.bmp",    "/down-80x80-bw-border.bmp",
  "/right-80x80-wb-border.bmp",   "/right-80x80-bw-border.bmp",
  "/left-80x80-wb-border.bmp",    "/left-80x80-bw-border.bmp"
};
static const uint8_t PRELOAD_ICON_COUNT = sizeof(PRELOAD_ICONS) / sizeof(PRELOAD_ICONS[0]);

// PHASE 2A: Validate cache size is sufficient for preloading
#if PRELOAD_ICON_COUNT > MAX_ICON_CACHE
#error "MAX_ICON_CACHE too small for preloading - increase in config.ino"
#endif

void SDManager_init() {
  SDManager_sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, SDManager_sdSPI, SD_SPI_HZ, "/sd", 5, false)) {
    Serial.println("[SD] Mount FAILED - using text fallbacks");
    SDManager_sdAvailable = false;
    return;
  }
  
  uint64_t cardSize = SD.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("[SD] Mount OK. Size: %llu MB, SPI: %d MHz\n", cardSize, SD_SPI_HZ / 1000000);
  SDManager_sdAvailable = true;
  
  // OPTIMIZED: Initialize cache metrics
  SDManager_totalCacheMemory = 0;
  SDManager_cacheHits = 0;
  SDManager_cacheMisses = 0;
  
  // PHASE 2A: Preload all icons at boot for zero-latency access
  SDManager_preloadAllIcons();
}

// PHASE 2A: Available if SD mounted OR assets are preloaded in cache
bool SDManager_isAvailable() {
  return SDManager_sdAvailable || SDManager_assetsPreloaded;
}

// OPTIMIZED: Fast cached icon rendering using drawRGBBitmap
bool SDManager_drawCachedIcon(const char* path, int16_t x, int16_t y) {
  uint32_t now = millis();
  
  for (int i = 0; i < SDManager_cacheCount; i++) {
    if (SDManager_iconCache[i].loaded && strcmp(SDManager_iconCache[i].path, path) == 0) {
      // OPTIMIZED: Update LRU timestamp
      SDManager_iconCache[i].lastUsed = now;
      SDManager_cacheHits++;
      
      // OPTIMIZED: Use bulk rendering instead of pixel-by-pixel
      auto& tft = DisplayManager_getTFT();
      tft.drawRGBBitmap(x, y, SDManager_iconCache[i].pixels, 
                       SDManager_iconCache[i].w, SDManager_iconCache[i].h);
      return true;
    }
  }
  
  SDManager_cacheMisses++;
  return false;
}

// OPTIMIZED: LRU cache eviction
void SDManager_evictLRUCache() {
  if (SDManager_cacheCount == 0) return;
  
  uint32_t oldestTime = UINT32_MAX;
  int oldestIndex = 0;
  
  for (int i = 0; i < SDManager_cacheCount; i++) {
    if (SDManager_iconCache[i].lastUsed < oldestTime) {
      oldestTime = SDManager_iconCache[i].lastUsed;
      oldestIndex = i;
    }
  }
  
  // Free memory
  if (SDManager_iconCache[oldestIndex].pixels) {
    SDManager_totalCacheMemory -= SDManager_iconCache[oldestIndex].memorySize;
    free(SDManager_iconCache[oldestIndex].pixels);
  }
  
  Serial.printf("[SD] Evicted cache[%d]: %s (%d bytes)\n", 
                oldestIndex, SDManager_iconCache[oldestIndex].path,
                SDManager_iconCache[oldestIndex].memorySize);
  
  // Shift remaining entries down
  for (int i = oldestIndex; i < SDManager_cacheCount - 1; i++) {
    SDManager_iconCache[i] = SDManager_iconCache[i + 1];
  }
  SDManager_cacheCount--;
}

// OPTIMIZED: Smart cache management with memory limits
void SDManager_cacheIcon(const char* path, uint16_t* pixels, int16_t w, int16_t h) {
  size_t bytes = (size_t)w * h * sizeof(uint16_t);
  
  // OPTIMIZED: Check memory limits before caching
  while ((SDManager_totalCacheMemory + bytes > MAX_CACHE_MEMORY || 
          SDManager_cacheCount >= MAX_ICON_CACHE) && 
         SDManager_cacheCount > 0) {
    SDManager_evictLRUCache();
  }
  
  if (SDManager_cacheCount >= MAX_ICON_CACHE) {
    Serial.println("[SD] Cache full - cannot add new icon");
    return;
  }
  
  int index = SDManager_cacheCount;
  strncpy(SDManager_iconCache[index].path, path, sizeof(SDManager_iconCache[index].path) - 1);
  SDManager_iconCache[index].path[sizeof(SDManager_iconCache[index].path) - 1] = 0;
  SDManager_iconCache[index].w = w; 
  SDManager_iconCache[index].h = h;
  SDManager_iconCache[index].lastUsed = millis();
  SDManager_iconCache[index].memorySize = bytes;
  
  // OPTIMIZED: Try ps_malloc first, then regular malloc
  SDManager_iconCache[index].pixels = (uint16_t*)ps_malloc(bytes);
  if (!SDManager_iconCache[index].pixels) {
    SDManager_iconCache[index].pixels = (uint16_t*)malloc(bytes);
  }
  
  if (!SDManager_iconCache[index].pixels) {
    Serial.printf("[SD] Failed to allocate %d bytes for icon cache\n", bytes);
    return;
  }
  
  memcpy(SDManager_iconCache[index].pixels, pixels, bytes);
  SDManager_iconCache[index].loaded = true;
  SDManager_totalCacheMemory += bytes;
  SDManager_cacheCount++;
  
  Serial.printf("[SD] Cached icon: %s (%dx%d, %d bytes) [%d/%d]\n", 
                path, w, h, bytes, SDManager_cacheCount, MAX_ICON_CACHE);
}

// PHASE 2A: Enhanced BMP loading - prioritize cache when SD unmounted
bool SDManager_drawBMPFromSD(const char* path, int16_t x, int16_t y) {
  // PHASE 2A: Always try cache first (especially when SD is unmounted after preload)
  if (SDManager_drawCachedIcon(path, x, y)) return true;
  
  // If assets are preloaded but icon not found in cache, it doesn't exist
  if (SDManager_assetsPreloaded && !SDManager_sdAvailable) {
    Serial.printf("[SD] PHASE 2A: Icon %s not in preloaded cache\n", path);
    return false;
  }
  
  // Fallback to SD loading if SD is still available
  if (!SDManager_sdAvailable) return false;
  
  // OPTIMIZED: Check heap health before loading
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < HEAP_WARNING_THRESHOLD) {
    Serial.printf("[SD] Low heap (%d bytes) - skipping BMP load\n", freeHeap);
    return false;
  }
  
  File f = SD.open(path);
  if (!f) { 
    Serial.printf("[BMP] open fail: %s\n", path); 
    return false; 
  }

  uint8_t hdr[54];
  if (f.read(hdr, 54) != 54 || hdr[0] != 'B' || hdr[1] != 'M') { 
    f.close(); 
    return false; 
  }

  uint32_t imageOffset = *(uint32_t*)&hdr[10];
  int32_t  width       = *(int32_t*)&hdr[18];
  int32_t  height      = *(int32_t*)&hdr[22];
  uint16_t bpp         = *(uint16_t*)&hdr[28];
  uint32_t comp        = *(uint32_t*)&hdr[30];
  
  if (comp != 0) { f.close(); return false; }
  if (width <= 0 || height <= 0 || width > SCREEN_WIDTH || height > SCREEN_HEIGHT) { 
    f.close(); 
    return false; 
  }

  // OPTIMIZED: Check if BMP is too large for available heap
  size_t pixelsN = (size_t)width * (size_t)height;
  size_t requiredBytes = pixelsN * sizeof(uint16_t);
  
  if (requiredBytes > LARGE_BMP_THRESHOLD || requiredBytes > ESP.getMaxAllocHeap()) {
    Serial.printf("[BMP] Too large: %dx%d (%d bytes) - max heap: %d\n", 
                  width, height, requiredBytes, ESP.getMaxAllocHeap());
    f.close();
    return false;
  }

  uint16_t* pix = (uint16_t*)ps_malloc(requiredBytes);
  if (!pix) pix = (uint16_t*)malloc(requiredBytes);
  if (!pix) { 
    Serial.printf("[BMP] Failed to allocate %d bytes\n", requiredBytes);
    f.close(); 
    return false; 
  }

  auto& tft = DisplayManager_getTFT();
  bool success = false;
  
  if (bpp == 8) {
    uint8_t pal[1024];
    f.seek(54);
    if (f.read(pal, 1024) == 1024) {
      uint32_t rowSize = ((width + 3) / 4) * 4;
      success = true;
      for (int row = height - 1; row >= 0 && success; row--) {
        f.seek(imageOffset + row * rowSize);
        for (int col = 0; col < width; col++) {
          int idx = f.read();
          if (idx < 0) { success = false; break; }
          uint8_t b = pal[idx * 4 + 0];
          uint8_t g = pal[idx * 4 + 1];
          uint8_t r = pal[idx * 4 + 2];
          pix[(height - 1 - row) * width + col] = tft.color565(r, g, b);
        }
      }
    }
  } else if (bpp == 24) {
    uint32_t rowSize = ((width * 3 + 3) / 4) * 4;
    success = true;
    for (int row = height - 1; row >= 0 && success; row--) {
      f.seek(imageOffset + row * rowSize);
      for (int col = 0; col < width; col++) {
        int b = f.read(), g = f.read(), r = f.read();
        if (b < 0 || g < 0 || r < 0) { success = false; break; }
        pix[(height - 1 - row) * width + col] = tft.color565(r, g, b);
      }
    }
  }

  f.close();

  if (success) {
    // OPTIMIZED: Use bulk rendering
    tft.drawRGBBitmap(x, y, pix, width, height);
    
    // Cache the icon for future use
    SDManager_cacheIcon(path, pix, width, height);
  } else {
    Serial.printf("[BMP] Failed to read pixel data: %s\n", path);
  }

  free(pix);
  return success;
}

// OPTIMIZED: Cache management functions
void SDManager_clearCache() {
  for (int i = 0; i < SDManager_cacheCount; i++) {
    if (SDManager_iconCache[i].pixels) {
      free(SDManager_iconCache[i].pixels);
      SDManager_iconCache[i].pixels = nullptr;
    }
    SDManager_iconCache[i].loaded = false;
  }
  SDManager_cacheCount = 0;
  SDManager_totalCacheMemory = 0;
  Serial.println("[SD] Cache cleared");
}

void SDManager_printCacheStats() {
  uint32_t total = SDManager_cacheHits + SDManager_cacheMisses;
  float hitRate = total > 0 ? (float)SDManager_cacheHits / total * 100.0f : 0.0f;
  
  Serial.printf("[SD] Cache: %d/%d entries, %d bytes, %.1f%% hit rate (%d/%d)\n",
                SDManager_cacheCount, MAX_ICON_CACHE, SDManager_totalCacheMemory,
                hitRate, SDManager_cacheHits, total);
}

// OPTIMIZED: Heap monitoring
void SDManager_checkHeapHealth() {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < HEAP_WARNING_THRESHOLD) {
    Serial.printf("[SD] Low heap warning: %d bytes free\n", freeHeap);
    
    // Emergency cache cleanup
    if (SDManager_cacheCount > MAX_ICON_CACHE / 2) {
      Serial.println("[SD] Emergency cache cleanup");
      // Clear half the cache (oldest entries)
      for (int i = 0; i < SDManager_cacheCount / 2; i++) {
        SDManager_evictLRUCache();
      }
    }
  }
}

// OPTIMIZED: Update function for periodic maintenance
void SDManager_update() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  
  if (now - lastUpdate > 10000) { // 10s intervals
    SDManager_checkHeapHealth();
    if ((now / 60000) % 5 == 0) { // Every 5 minutes
      SDManager_printCacheStats();
    }
    lastUpdate = now;
  }
}

// PHASE 2A: Preload all UI icons at boot and unmount SD
void SDManager_preloadAllIcons() {
  if (!SDManager_sdAvailable) {
    Serial.println("[SD] Cannot preload - SD not available");
    return;
  }
  
  Serial.printf("[SD] PHASE 2A: Preloading %d icons for zero-latency UI...\n", PRELOAD_ICON_COUNT);
  Serial.printf("[SD] Cache limits: %d slots, %d KB memory\n", MAX_ICON_CACHE, MAX_CACHE_MEMORY / 1000);
  uint32_t preloadStart = millis();
  uint8_t successCount = 0;
  
  for (uint8_t i = 0; i < PRELOAD_ICON_COUNT; i++) {
    // Force load each icon (this will cache it)
    Serial.printf("[SD] Preloading [%d/%d]: %s\n", i + 1, PRELOAD_ICON_COUNT, PRELOAD_ICONS[i]);
    
    if (SDManager_forceLoadIconToCache(PRELOAD_ICONS[i])) {
      successCount++;
      Serial.printf("[SD] ✓ Success [%d/%d slots, %d KB used]\n", SDManager_cacheCount, MAX_ICON_CACHE, SDManager_totalCacheMemory/1000);
    } else {
      Serial.printf("[SD] Failed to preload: %s\n", PRELOAD_ICONS[i]);
    }
    
    // Yield periodically to prevent watchdog timeout
    if ((i & 3) == 0) delay(0);
  }
  
  uint32_t preloadTime = millis() - preloadStart;
  Serial.printf("[SD] PHASE 2A: Preloaded %d/%d icons in %dms\n", successCount, PRELOAD_ICON_COUNT, preloadTime);
  Serial.printf("[SD] Cache usage: %d bytes (%.1f%% full)\n", SDManager_totalCacheMemory, 
                (float)SDManager_totalCacheMemory / MAX_CACHE_MEMORY * 100.0f);
  
  // PHASE 2B: Enhanced unmounting with bus conflict prevention
  if (successCount == PRELOAD_ICON_COUNT) {
    Serial.println("[SD] PHASE 2B: All icons loaded - unmounting SD card to free SPI bus");
    SD.end();
    SDManager_sdAvailable = false;
    SDManager_assetsPreloaded = true;
    
    Serial.println("[SD] PHASE 2B: Complete - UI now operates with zero-latency cached assets");
    Serial.printf("[SD] SPI bus released - potential bus conflicts eliminated\n");
  } else {
    Serial.printf("[SD] PHASE 2B: Only %d/%d icons loaded - keeping SD mounted\n", successCount, PRELOAD_ICON_COUNT);
    SDManager_assetsPreloaded = false;
  }
}

// PHASE 2A: Force load an icon directly to cache (used during preload)
bool SDManager_forceLoadIconToCache(const char* path) {
  if (!SDManager_sdAvailable) return false;
  
  // Check if already cached
  for (int i = 0; i < SDManager_cacheCount; i++) {
    if (SDManager_iconCache[i].loaded && strcmp(SDManager_iconCache[i].path, path) == 0) {
      return true; // Already cached
    }
  }
  
  File f = SD.open(path);
  if (!f) { 
    Serial.printf("[SD] PRELOAD: Cannot open %s\n", path); 
    return false; 
  }

  uint8_t hdr[54];
  if (f.read(hdr, 54) != 54 || hdr[0] != 'B' || hdr[1] != 'M') { 
    f.close(); 
    return false; 
  }

  uint32_t imageOffset = *(uint32_t*)&hdr[10];
  int32_t  width       = *(int32_t*)&hdr[18];
  int32_t  height      = *(int32_t*)&hdr[22];
  uint16_t bpp         = *(uint16_t*)&hdr[28];
  uint32_t comp        = *(uint32_t*)&hdr[30];
  
  if (comp != 0 || width <= 0 || height <= 0 || width > SCREEN_WIDTH || height > SCREEN_HEIGHT) { 
    f.close(); 
    return false; 
  }

  size_t pixelsN = (size_t)width * (size_t)height;
  size_t requiredBytes = pixelsN * sizeof(uint16_t);
  
  // PHASE 2A: Check memory limits - fail fast during preload to avoid eviction
  while ((SDManager_totalCacheMemory + requiredBytes > MAX_CACHE_MEMORY || 
          SDManager_cacheCount >= MAX_ICON_CACHE) && 
         SDManager_cacheCount > 0) {
    SDManager_evictLRUCache();
  }
  
  if (SDManager_cacheCount >= MAX_ICON_CACHE) {
    f.close();
    return false;
  }

  uint16_t* pix = (uint16_t*)ps_malloc(requiredBytes);
  if (!pix) pix = (uint16_t*)malloc(requiredBytes);
  if (!pix) { 
    f.close(); 
    return false; 
  }

  auto& tft = DisplayManager_getTFT();
  bool success = false;
  
  if (bpp == 8) {
    uint8_t pal[1024];
    f.seek(54);
    if (f.read(pal, 1024) == 1024) {
      uint32_t rowSize = ((width + 3) / 4) * 4;
      success = true;
      for (int row = height - 1; row >= 0 && success; row--) {
        f.seek(imageOffset + row * rowSize);
        for (int col = 0; col < width; col++) {
          int idx = f.read();
          if (idx < 0) { success = false; break; }
          uint8_t b = pal[idx * 4 + 0];
          uint8_t g = pal[idx * 4 + 1];
          uint8_t r = pal[idx * 4 + 2];
          pix[(height - 1 - row) * width + col] = tft.color565(r, g, b);
        }
      }
    }
  } else if (bpp == 24) {
    uint32_t rowSize = ((width * 3 + 3) / 4) * 4;
    success = true;
    for (int row = height - 1; row >= 0 && success; row--) {
      f.seek(imageOffset + row * rowSize);
      for (int col = 0; col < width; col++) {
        int b = f.read(), g = f.read(), r = f.read();
        if (b < 0 || g < 0 || r < 0) { success = false; break; }
        pix[(height - 1 - row) * width + col] = tft.color565(r, g, b);
      }
    }
  }

  f.close();

  if (success) {
    // Cache the icon directly without rendering
    SDManager_cacheIcon(path, pix, width, height);
  }

  free(pix);
  return success;
}

bool SDManager_areAssetsPreloaded() {
  return SDManager_assetsPreloaded;
}

size_t SDManager_getCacheMemoryUsage() {
  return SDManager_totalCacheMemory;
}

int SDManager_getCacheCount() {
  return SDManager_cacheCount;
}

float SDManager_getCacheHitRate() {
  uint32_t total = SDManager_cacheHits + SDManager_cacheMisses;
  return total > 0 ? (float)SDManager_cacheHits / total : 0.0f;
}