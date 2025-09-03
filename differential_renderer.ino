// ─────────────────────────────────────────────────────────────
// Differential Rendering Engine - PHASE 4: Optimized Display Updates
// Tracks and renders only changed screen regions for maximum efficiency
// ─────────────────────────────────────────────────────────────

#if ENABLE_DIFFERENTIAL_RENDERING

// ───── Rendering Configuration ─────
#define MAX_SCREEN_REGIONS 16           // Maximum tracked screen regions
#define REGION_MERGE_THRESHOLD 20       // Merge regions if closer than 20 pixels
#define VSYNC_TARGET_FPS 60             // Target 60 FPS with vsync
#define FRAME_TIME_BUDGET_US 33333      // 33.33ms frame budget (30 FPS) - more relaxed
#define RENDER_QUEUE_SIZE 8             // Maximum queued render operations

// ───── Screen Region Structure ─────
typedef struct {
  int16_t x, y, w, h;        // Region bounds
  bool dirty;                // Needs redrawing
  uint8_t priority;          // Render priority (0=highest, 255=lowest)
  uint32_t lastUpdate;       // When region was last rendered
  uint32_t changeFrequency;  // How often this region changes
  bool persistent;           // Always track this region
} ScreenRegion;

// ───── Render Operation Structure ─────
typedef struct {
  int16_t x, y, w, h;        // Render area
  uint8_t operation;         // Render operation type
  uint32_t timestamp;        // When operation was queued
  void* data;                // Optional data pointer
  bool completed;            // Operation completed
} RenderOperation;

// RenderOperationType enum is now defined in the main .ino file

// ───── Frame Buffer Management ─────
typedef struct {
  uint16_t* frontBuffer;     // Currently displayed buffer
  uint16_t* backBuffer;      // Being rendered to
  uint16_t* regionBuffer;    // Temporary region buffer
  bool doubleBuffered;       // Double buffering active
  bool bufferSwapPending;    // Swap needed
  uint32_t lastSwap;         // Last buffer swap time
} FrameBufferContext;

// ───── Vsync and Timing ─────
typedef struct {
  uint32_t lastVsync;        // Last vsync timestamp
  uint32_t frameStartTime;   // Current frame start time
  uint32_t renderStartTime;  // Render phase start time
  uint32_t avgFrameTime;     // Average frame time
  uint32_t frameBudgetUsed;  // Time used in current frame
  bool vsyncEnabled;         // Vsync active
  uint8_t missedFrames;      // Consecutive missed frame budgets
} VsyncContext;

// ───── Main Rendering Context ─────
typedef struct {
  ScreenRegion regions[MAX_SCREEN_REGIONS];
  uint8_t regionCount;
  RenderOperation renderQueue[RENDER_QUEUE_SIZE];
  uint8_t queueHead, queueTail, queueCount;
  FrameBufferContext frameBuffer;
  VsyncContext vsync;
  uint32_t totalPixelsRendered;
  uint32_t totalRenderOperations;
  uint32_t regionsOptimizedOut;
  bool enabled;
  bool debugVisualization;
} DifferentialRenderContext;

// ───── Global State ─────
static DifferentialRenderContext DRender_context = {0};
static bool DRender_initialized = false;

// Forward declarations
void DisplayManager_setWindow(int16_t x, int16_t y, int16_t w, int16_t h);
void DisplayManager_pushColors(uint16_t* colors, uint32_t len);
void DisplayManager_startWrite();
void DisplayManager_endWrite();
uint16_t* DisplayManager_getFrameBuffer();
void DisplayManager_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void FreqManager_notifyDisplayActivity();

// ───── Region Management ─────
uint8_t DRender_createRegion(int16_t x, int16_t y, int16_t w, int16_t h, bool persistent) {
  if (DRender_context.regionCount >= MAX_SCREEN_REGIONS) {
    Serial.println("[DRENDER] Max regions reached");
    return 255; // Invalid index
  }
  
  uint8_t index = DRender_context.regionCount++;
  ScreenRegion* region = &DRender_context.regions[index];
  
  region->x = max((int16_t)0, min((int16_t)x, (int16_t)SCREEN_WIDTH));
  region->y = max((int16_t)0, min((int16_t)y, (int16_t)SCREEN_HEIGHT));
  region->w = max((int16_t)1, min((int16_t)w, (int16_t)(SCREEN_WIDTH - region->x)));
  region->h = max((int16_t)1, min((int16_t)h, (int16_t)(SCREEN_HEIGHT - region->y)));
  region->dirty = true;
  region->priority = 128; // Default priority
  region->lastUpdate = 0;
  region->changeFrequency = 0;
  region->persistent = persistent;
  
  Serial.printf("[DRENDER] Created region %d: (%d,%d,%d,%d) %s\n", 
                index, region->x, region->y, region->w, region->h, 
                persistent ? "persistent" : "temporary");
  
  return index;
}

void DRender_markRegionDirty(uint8_t regionIndex) {
  if (regionIndex >= DRender_context.regionCount) return;
  
  ScreenRegion* region = &DRender_context.regions[regionIndex];
  if (!region->dirty) {
    region->dirty = true;
    region->changeFrequency++;
    
    Serial.printf("[DRENDER] Region %d marked dirty (freq=%d)\n", 
                  regionIndex, region->changeFrequency);
  }
}

void DRender_markAreaDirty(int16_t x, int16_t y, int16_t w, int16_t h) {
  // Find all regions that overlap with the dirty area
  for (uint8_t i = 0; i < DRender_context.regionCount; i++) {
    ScreenRegion* region = &DRender_context.regions[i];
    
    // Check for overlap
    if (!(x >= region->x + region->w || x + w <= region->x ||
          y >= region->y + region->h || y + h <= region->y)) {
      DRender_markRegionDirty(i);
    }
  }
}

void DRender_optimizeRegions() {
  // Merge nearby regions to reduce render calls
  for (uint8_t i = 0; i < DRender_context.regionCount - 1; i++) {
    ScreenRegion* regionA = &DRender_context.regions[i];
    if (!regionA->dirty) continue;
    
    for (uint8_t j = i + 1; j < DRender_context.regionCount; j++) {
      ScreenRegion* regionB = &DRender_context.regions[j];
      if (!regionB->dirty) continue;
      
      // Calculate distance between regions
      int16_t dx = max(0, max(regionA->x - (regionB->x + regionB->w), regionB->x - (regionA->x + regionA->w)));
      int16_t dy = max(0, max(regionA->y - (regionB->y + regionB->h), regionB->y - (regionA->y + regionA->h)));
      int16_t distance = sqrt(dx * dx + dy * dy);
      
      if (distance <= REGION_MERGE_THRESHOLD) {
        // Merge regions
        int16_t minX = min(regionA->x, regionB->x);
        int16_t minY = min(regionA->y, regionB->y);
        int16_t maxX = max(regionA->x + regionA->w, regionB->x + regionB->w);
        int16_t maxY = max(regionA->y + regionA->h, regionB->y + regionB->h);
        
        regionA->x = minX;
        regionA->y = minY;
        regionA->w = maxX - minX;
        regionA->h = maxY - minY;
        regionA->priority = min(regionA->priority, regionB->priority);
        
        // Remove region B
        for (uint8_t k = j; k < DRender_context.regionCount - 1; k++) {
          DRender_context.regions[k] = DRender_context.regions[k + 1];
        }
        DRender_context.regionCount--;
        j--; // Re-check this position
        
        Serial.printf("[DRENDER] Merged regions -> (%d,%d,%d,%d)\n", 
                      regionA->x, regionA->y, regionA->w, regionA->h);
      }
    }
  }
}

// ───── Frame Buffer Management ─────
bool DRender_initFrameBuffers() {
  FrameBufferContext* fb = &DRender_context.frameBuffer;
  
  size_t bufferSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2; // 16-bit color
  
  // CRITICAL FIX: Don't allocate large frame buffers that cause memory corruption
  // Instead, use a reduced mode that works with direct rendering
  Serial.printf("[DRENDER] Frame buffer size would be: %d bytes\n", bufferSize);
  
  // Check available PSRAM/heap before allocation
  size_t availablePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t availableHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  
  Serial.printf("[DRENDER] Available PSRAM: %d bytes, Heap: %d bytes\n", availablePSRAM, availableHeap);
  
  // Only try to allocate if we have sufficient memory with safety margin
  if (availablePSRAM > (bufferSize * 2 + 32768)) { // 32KB safety margin
    fb->frontBuffer = (uint16_t*)ps_malloc(bufferSize);
    if (fb->frontBuffer) {
      fb->backBuffer = (uint16_t*)ps_malloc(bufferSize);
      if (fb->backBuffer) {
        fb->doubleBuffered = true;
        memset(fb->frontBuffer, 0, bufferSize);
        memset(fb->backBuffer, 0, bufferSize);
        Serial.println("[DRENDER] Double buffering enabled");
      } else {
        free(fb->frontBuffer);
        fb->frontBuffer = nullptr;
        fb->doubleBuffered = false;
        Serial.println("[DRENDER] Insufficient memory for double buffering");
      }
    }
  } else {
    Serial.println("[DRENDER] Insufficient PSRAM for frame buffers - using direct mode");
    fb->frontBuffer = nullptr;
    fb->backBuffer = nullptr;
    fb->doubleBuffered = false;
  }
  
  // Always succeed - direct rendering mode is safe fallback
  Serial.printf("[DRENDER] Frame buffers initialized: %s buffered\n", 
                fb->doubleBuffered ? "double" : "direct");
  
  return true;
}

void DRender_swapBuffers() {
  FrameBufferContext* fb = &DRender_context.frameBuffer;
  
  if (fb->doubleBuffered && fb->bufferSwapPending) {
    // Swap front and back buffers
    uint16_t* temp = fb->frontBuffer;
    fb->frontBuffer = fb->backBuffer;
    fb->backBuffer = temp;
    
    fb->bufferSwapPending = false;
    fb->lastSwap = micros();
    
    Serial.println("[DRENDER] Buffers swapped");
  }
}

// ───── Vsync and Frame Timing ─────
void DRender_waitForVsync() {
  VsyncContext* vsync = &DRender_context.vsync;
  
  if (!vsync->vsyncEnabled) return;
  
  uint32_t now = micros();
  uint32_t frameTime = FRAME_TIME_BUDGET_US;
  uint32_t elapsed = now - vsync->frameStartTime;
  
  if (elapsed < frameTime) {
    // Wait for next frame boundary
    uint32_t waitTime = frameTime - elapsed;
    if (waitTime > 1000) { // Only wait if more than 1ms
      delayMicroseconds(waitTime);
    }
  } else if (elapsed > frameTime * 1.1) { // 10% tolerance
    vsync->missedFrames++;
  }
  
  vsync->lastVsync = micros();
  vsync->frameStartTime = vsync->lastVsync;
  
  // Update average frame time
  vsync->avgFrameTime = (vsync->avgFrameTime * 7 + elapsed) / 8;
}

void DRender_startFrame() {
  VsyncContext* vsync = &DRender_context.vsync;
  
  vsync->frameStartTime = micros();
  vsync->renderStartTime = vsync->frameStartTime;
  vsync->frameBudgetUsed = 0;
  
  FreqManager_notifyDisplayActivity();
}

bool DRender_checkFrameBudget(uint32_t estimatedTime) {
  VsyncContext* vsync = &DRender_context.vsync;
  
  uint32_t now = micros();
  uint32_t elapsed = now - vsync->frameStartTime;
  
  return (elapsed + estimatedTime) < FRAME_TIME_BUDGET_US;
}

// ───── Render Queue Management ─────
bool DRender_queueOperation(RenderOperationType operation, int16_t x, int16_t y, int16_t w, int16_t h, void* data) {
  if (DRender_context.queueCount >= RENDER_QUEUE_SIZE) {
    Serial.println("[DRENDER] Render queue full");
    return false;
  }
  
  RenderOperation* op = &DRender_context.renderQueue[DRender_context.queueTail];
  
  op->x = x;
  op->y = y;
  op->w = w;
  op->h = h;
  op->operation = operation;
  op->timestamp = micros();
  op->data = data;
  op->completed = false;
  
  DRender_context.queueTail = (DRender_context.queueTail + 1) % RENDER_QUEUE_SIZE;
  DRender_context.queueCount++;
  
  return true;
}

void DRender_processRenderQueue() {
  while (DRender_context.queueCount > 0 && DRender_checkFrameBudget(2000)) { // Reserve 2ms budget
    RenderOperation* op = &DRender_context.renderQueue[DRender_context.queueHead];
    
    uint32_t opStartTime = micros();
    
    // Execute render operation
    switch (op->operation) {
      case RENDER_FULL_REFRESH:
        DRender_performFullRefresh();
        break;
        
      case RENDER_REGION_UPDATE:
        DRender_renderRegion(op->x, op->y, op->w, op->h);
        break;
        
      case RENDER_ICON_BLIT:
        DRender_blitIcon(op->x, op->y, op->w, op->h, (uint16_t*)op->data);
        break;
        
      case RENDER_TEXT_UPDATE:
        DRender_renderText(op->x, op->y, (char*)op->data);
        break;
        
      case RENDER_PROGRESS_BAR:
        DRender_renderProgressBar(op->x, op->y, op->w, op->h, *((uint8_t*)op->data));
        break;
        
      case RENDER_ANIMATION_FRAME:
        DRender_renderAnimationFrame(op->x, op->y, op->w, op->h, op->data);
        break;
    }
    
    uint32_t opTime = micros() - opStartTime;
    DRender_context.vsync.frameBudgetUsed += opTime;
    
    // Mark operation as completed
    op->completed = true;
    DRender_context.queueHead = (DRender_context.queueHead + 1) % RENDER_QUEUE_SIZE;
    DRender_context.queueCount--;
    
    DRender_context.totalRenderOperations++;
  }
}

// ───── Core Rendering Functions ─────
void DRender_renderRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
  FrameBufferContext* fb = &DRender_context.frameBuffer;
  
  // CRITICAL FIX: Validate region bounds before any operations
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || 
      x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT ||
      (x + w) > SCREEN_WIDTH || (y + h) > SCREEN_HEIGHT) {
    Serial.printf("[DRENDER] ERROR: Invalid region bounds (%d,%d,%d,%d) - skipping\n", x, y, w, h);
    return;
  }
  
  DisplayManager_startWrite();
  
  // CRITICAL FIX: Since DisplayManager_getFrameBuffer() returns null,
  // we cannot do differential rendering. Use direct fill operations instead.
  if (fb->doubleBuffered && fb->backBuffer) {
    // Use back buffer if available
    DisplayManager_setWindow(x, y, w, h);
    uint16_t* pixels = fb->backBuffer + (y * SCREEN_WIDTH) + x;
    for (int16_t row = 0; row < h; row++) {
      DisplayManager_pushColors(pixels + (row * SCREEN_WIDTH), w);
    }
  } else {
    // SAFE FALLBACK: Use direct fill to prevent corruption
    // This eliminates the null pointer access that was causing crashes
    Serial.printf("[DRENDER] SAFE MODE: Filling region %d,%d %dx%d with black\n", x, y, w, h);
    DisplayManager_fillRect(x, y, w, h, 0x0000);
  }
  
  DisplayManager_endWrite();
  
  DRender_context.totalPixelsRendered += w * h;
}

void DRender_performFullRefresh() {
  Serial.println("[DRENDER] Performing full screen refresh");
  
  // Mark all regions as dirty
  for (uint8_t i = 0; i < DRender_context.regionCount; i++) {
    DRender_context.regions[i].dirty = true;
  }
  
  // Render entire screen
  DRender_renderRegion(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  
  // Clear all dirty flags
  for (uint8_t i = 0; i < DRender_context.regionCount; i++) {
    DRender_context.regions[i].dirty = false;
    DRender_context.regions[i].lastUpdate = millis();
  }
}

void DRender_blitIcon(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* iconData) {
  if (!iconData) return;
  
  DisplayManager_startWrite();
  DisplayManager_setWindow(x, y, w, h);
  DisplayManager_pushColors(iconData, w * h);
  DisplayManager_endWrite();
  
  DRender_context.totalPixelsRendered += w * h;
}

void DRender_renderText(int16_t x, int16_t y, const char* text) {
  // Placeholder for text rendering integration
  // This would integrate with the existing text rendering system
  Serial.printf("[DRENDER] Text render at (%d,%d): %s\n", x, y, text);
}

void DRender_renderProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t progress) {
  int16_t fillWidth = (w * progress) / 100;
  
  DisplayManager_startWrite();
  
  // Fill progress portion
  if (fillWidth > 0) {
    DisplayManager_fillRect(x, y, fillWidth, h, 0x07E0); // Green
  }
  
  // Fill remaining portion
  if (fillWidth < w) {
    DisplayManager_fillRect(x + fillWidth, y, w - fillWidth, h, 0x7BEF); // Light gray
  }
  
  DisplayManager_endWrite();
  
  DRender_context.totalPixelsRendered += w * h;
}

void DRender_renderAnimationFrame(int16_t x, int16_t y, int16_t w, int16_t h, void* frameData) {
  // Placeholder for animation frame rendering
  // This would integrate with the animation system
  Serial.printf("[DRENDER] Animation frame at (%d,%d,%d,%d)\n", x, y, w, h);
}

// ───── Main Differential Rendering Loop ─────
void DRender_update() {
  if (!ENABLE_DIFFERENTIAL_RENDERING || !DRender_initialized || !DRender_context.enabled) return;
  
  DRender_startFrame();
  
  // Optimize regions before rendering
  DRender_optimizeRegions();
  
  // Sort regions by priority (highest priority first)
  DRender_sortRegionsByPriority();
  
  // Process render queue
  DRender_processRenderQueue();
  
  // Render dirty regions
  uint8_t regionsRendered = 0;
  for (uint8_t i = 0; i < DRender_context.regionCount; i++) {
    ScreenRegion* region = &DRender_context.regions[i];
    
    if (region->dirty && DRender_checkFrameBudget(3000)) { // Estimate 3ms per region
      DRender_renderRegion(region->x, region->y, region->w, region->h);
      region->dirty = false;
      region->lastUpdate = millis();
      regionsRendered++;
    } else if (region->dirty) {
      // Frame budget exceeded - defer to next frame
      // Note: This is normal behavior, no need to log
      break;
    }
  }
  
  // Swap buffers if using double buffering
  if (DRender_context.frameBuffer.doubleBuffered && regionsRendered > 0) {
    DRender_context.frameBuffer.bufferSwapPending = true;
    DRender_swapBuffers();
  }
  
  // Wait for vsync if enabled
  DRender_waitForVsync();
  
  // Clean up old temporary regions
  DRender_cleanupRegions();
}

void DRender_sortRegionsByPriority() {
  // Simple bubble sort by priority (good enough for small arrays)
  for (uint8_t i = 0; i < DRender_context.regionCount - 1; i++) {
    for (uint8_t j = 0; j < DRender_context.regionCount - i - 1; j++) {
      if (DRender_context.regions[j].priority > DRender_context.regions[j + 1].priority) {
        ScreenRegion temp = DRender_context.regions[j];
        DRender_context.regions[j] = DRender_context.regions[j + 1];
        DRender_context.regions[j + 1] = temp;
      }
    }
  }
}

void DRender_cleanupRegions() {
  uint32_t now = millis();
  
  for (uint8_t i = 0; i < DRender_context.regionCount; i++) {
    ScreenRegion* region = &DRender_context.regions[i];
    
    // Remove temporary regions that haven't been updated in 5 seconds
    if (!region->persistent && (now - region->lastUpdate) > 5000 && !region->dirty) {
      Serial.printf("[DRENDER] Cleaning up region %d\n", i);
      
      // Move last region to this position
      if (i < DRender_context.regionCount - 1) {
        DRender_context.regions[i] = DRender_context.regions[DRender_context.regionCount - 1];
      }
      DRender_context.regionCount--;
      i--; // Re-check this position
    }
  }
}

// ───── Core Functions ─────
void DRender_init() {
  if (!ENABLE_DIFFERENTIAL_RENDERING) {
    Serial.println("[DRENDER] Differential rendering disabled");
    return;
  }
  
  Serial.println("[DRENDER] Initializing differential rendering engine...");
  
  // Initialize context
  memset(&DRender_context, 0, sizeof(DifferentialRenderContext));
  
  // Initialize frame buffers
  if (!DRender_initFrameBuffers()) {
    Serial.println("[DRENDER] Failed to initialize frame buffers");
    return;
  }
  
  // Initialize vsync context
  DRender_context.vsync.vsyncEnabled = true;
  DRender_context.vsync.frameStartTime = micros();
  
  // Create default screen regions for common UI elements with full coverage
  DRender_createRegion(0, 0, SCREEN_WIDTH, 60, true);        // Header area (extended to 60)
  DRender_createRegion(0, SCREEN_HEIGHT - 60, SCREEN_WIDTH, 60, true); // Footer area (starts at 260)
  DRender_createRegion(0, 60, SCREEN_WIDTH, SCREEN_HEIGHT - 120, true); // Content area (full width, 60-260)
  
  DRender_context.enabled = true;
  DRender_initialized = true;
  
  Serial.printf("[DRENDER] Initialized with %d regions, %s buffering, vsync %s\n",
                DRender_context.regionCount,
                DRender_context.frameBuffer.doubleBuffered ? "double" : "single",
                DRender_context.vsync.vsyncEnabled ? "enabled" : "disabled");
}

// ───── Control and Status Functions ─────
void DRender_enable(bool enabled) {
  DRender_context.enabled = enabled;
  Serial.printf("[DRENDER] Differential rendering %s\n", enabled ? "enabled" : "disabled");
}

bool DRender_isEnabled() {
  return ENABLE_DIFFERENTIAL_RENDERING && DRender_context.enabled;
}

void DRender_forceFullRefresh() {
  DRender_queueOperation(RENDER_FULL_REFRESH, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, nullptr);
}

// ───── Statistics and Debugging ─────
void DRender_printStats() {
  if (!ENABLE_DIFFERENTIAL_RENDERING) {
    Serial.println("[DRENDER] Differential rendering disabled");
    return;
  }
  
  VsyncContext* vsync = &DRender_context.vsync;
  
  Serial.printf("[DRENDER] Regions: %d active, %d operations, %d pixels rendered\n",
                DRender_context.regionCount, DRender_context.totalRenderOperations,
                DRender_context.totalPixelsRendered);
  Serial.printf("[DRENDER] Frame timing: avg=%.2fms, missed=%d, queue=%d/%d\n",
                vsync->avgFrameTime / 1000.0f, vsync->missedFrames,
                DRender_context.queueCount, RENDER_QUEUE_SIZE);
  Serial.printf("[DRENDER] Buffer mode: %s, vsync: %s\n",
                DRender_context.frameBuffer.doubleBuffered ? "double" : "single",
                vsync->vsyncEnabled ? "enabled" : "disabled");
  
  // Show region details
  Serial.println("[DRENDER] Active regions:");
  for (uint8_t i = 0; i < min((uint8_t)8, DRender_context.regionCount); i++) {
    ScreenRegion* region = &DRender_context.regions[i];
    Serial.printf("  %d: (%d,%d,%d,%d) %s priority=%d freq=%d\n",
                  i, region->x, region->y, region->w, region->h,
                  region->dirty ? "DIRTY" : "clean", region->priority, region->changeFrequency);
  }
}

float DRender_getFrameRate() {
  if (DRender_context.vsync.avgFrameTime == 0) return 0.0f;
  return 1000000.0f / DRender_context.vsync.avgFrameTime;
}

uint32_t DRender_getTotalPixelsRendered() {
  return DRender_context.totalPixelsRendered;
}

uint8_t DRender_getRegionCount() {
  return DRender_context.regionCount;
}

#else // !ENABLE_DIFFERENTIAL_RENDERING

// ───── Stub Functions When Differential Rendering is Disabled ─────
void DRender_init() {}
void DRender_update() {}
uint8_t DRender_createRegion(int16_t x, int16_t y, int16_t w, int16_t h, bool persistent) { return 255; }
void DRender_markRegionDirty(uint8_t regionIndex) {}
void DRender_markAreaDirty(int16_t x, int16_t y, int16_t w, int16_t h) {}
void DRender_enable(bool enabled) {}
bool DRender_isEnabled() { return false; }
void DRender_forceFullRefresh() {}
void DRender_printStats() { Serial.println("[DRENDER] Differential rendering disabled"); }
float DRender_getFrameRate() { return 0.0f; }
uint32_t DRender_getTotalPixelsRendered() { return 0; }
uint8_t DRender_getRegionCount() { return 0; }

#endif // ENABLE_DIFFERENTIAL_RENDERING