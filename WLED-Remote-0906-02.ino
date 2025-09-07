// ─────────────────────────────────────────────────────────────
// WLED Remote Control for ESP32-S3-Touch-2.8 - Enhanced Main Entry Point
// Now includes brightness page control and comprehensive power management
// ─────────────────────────────────────────────────────────────

// CRITICAL: Define config constants here to ensure availability before use
// These mirror the definitions in config.ino but are needed earlier
#define ENABLE_BRIGHTNESS_PAGE false
#define ENABLE_WLED_SELECTION_PAGE true   // Re-enabled: not the crash source
#define ENABLE_NOW_PLAYING_PAGE true    // Re-enabled: crashes fixed by guarding advanced features

// Forward declare config variables and functions
extern uint8_t CURRENT_WLED_INSTANCE;
extern const uint8_t WLED_INSTANCE_COUNT;
void ensureValidWLEDInstance();
const char* getWLEDIP();

// Calculate total pages based on enabled optional pages
// Page layout: 0=Main, 1=Navigation, 2=Now Playing, 3=Brightness, 4=System, 5=WLED Selection
#if ENABLE_NOW_PLAYING_PAGE && ENABLE_BRIGHTNESS_PAGE && ENABLE_WLED_SELECTION_PAGE
static const int TOTAL_UI_PAGES = 6;  // All pages enabled: Main, Nav, Now Playing, Brightness, System, WLED
#elif ENABLE_NOW_PLAYING_PAGE && ENABLE_BRIGHTNESS_PAGE && !ENABLE_WLED_SELECTION_PAGE
static const int TOTAL_UI_PAGES = 5;  // Main, Nav, Now Playing, Brightness, System
#elif ENABLE_NOW_PLAYING_PAGE && !ENABLE_BRIGHTNESS_PAGE && ENABLE_WLED_SELECTION_PAGE
static const int TOTAL_UI_PAGES = 5;  // Main, Nav, Now Playing, System, WLED
#elif ENABLE_NOW_PLAYING_PAGE && !ENABLE_BRIGHTNESS_PAGE && !ENABLE_WLED_SELECTION_PAGE
static const int TOTAL_UI_PAGES = 4;  // Main, Nav, Now Playing (with instance dropdown), System
#elif !ENABLE_NOW_PLAYING_PAGE && ENABLE_BRIGHTNESS_PAGE && ENABLE_WLED_SELECTION_PAGE
static const int TOTAL_UI_PAGES = 5;  // Main, Nav, Brightness, System, WLED
#elif !ENABLE_NOW_PLAYING_PAGE && ENABLE_BRIGHTNESS_PAGE && !ENABLE_WLED_SELECTION_PAGE
static const int TOTAL_UI_PAGES = 4;  // Main, Nav, Brightness, System
#elif !ENABLE_NOW_PLAYING_PAGE && !ENABLE_BRIGHTNESS_PAGE && ENABLE_WLED_SELECTION_PAGE
static const int TOTAL_UI_PAGES = 4;  // Main, Nav, System, WLED
#else
static const int TOTAL_UI_PAGES = 3;  // Main, Nav, System (minimum configuration)
#endif

#include <WiFi.h>  // Need this for WiFi.RSSI() call in loop()
#include <Adafruit_ST7789.h>  // Need this for DisplayManager_getTFT() return type
#include <HTTPClient.h>  // Need this for TwistManager HTTP requests
#include <ArduinoJson.h>  // Need this for TwistManager JSON parsing
#include <CSE_CST328.h>  // Need this for TouchManager

// ───── ENUM AND STRUCT DEFINITIONS ─────
// These must be defined here to be available to all .ino files

// WLED Client structs
struct PaletteColorData {
  uint8_t r, g, b;
  bool valid;
};

// UI Manager structs
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

// Frequency Manager enums
enum FreqScalingMode {
  FREQ_MODE_HIGH = 0,    // 240MHz - High performance for active UI interaction
  FREQ_MODE_NORMAL = 1,  // 160MHz - Normal operation for background tasks  
  FREQ_MODE_LOW = 2      // 80MHz - Power saving for idle periods
};

// Intelligent Power enums
enum IntelligentPowerState {
  IPOWER_ACTIVE = 0,          // Full performance - user actively interacting
  IPOWER_INTERACTIVE = 1,     // Normal performance - brief pauses in interaction
  IPOWER_IDLE = 2,           // Reduced performance - no recent interaction
  IPOWER_STANDBY = 3,        // Minimal performance - screen off, monitoring only
  IPOWER_DEEP_SLEEP = 4      // Ultra-low power - wake on touch/network event
};

// Differential Renderer enums
enum RenderOperationType {
  RENDER_FULL_REFRESH = 1,   // Full screen refresh
  RENDER_REGION_UPDATE = 2,  // Update specific region
  RENDER_ICON_BLIT = 3,      // Fast icon blit
  RENDER_TEXT_UPDATE = 4,    // Text rendering
  RENDER_PROGRESS_BAR = 5,   // Progress/slider update
  RENDER_ANIMATION_FRAME = 6 // Animation frame update
};

// Network Task enums
enum NetworkCommandType {
  NET_CMD_PRESET = 1,
  NET_CMD_QUICKLOAD = 2,
  NET_CMD_BRIGHTNESS = 3,
  NET_CMD_POWER_TOGGLE = 4,
  NET_CMD_PRESET_CYCLE = 5,
  NET_CMD_PALETTE_CYCLE = 6,
  NET_CMD_SYNC_STATE = 7,
  NET_CMD_WIFI_RECONNECT = 8
};

// Predictive Scaling enums
enum PerformanceEventType {
  PERF_TOUCH_BURST = 1,      // Rapid touch interactions
  PERF_NETWORK_BURST = 2,    // Network command bursts
  PERF_DISPLAY_INTENSIVE = 3, // Heavy display rendering
  PERF_UI_TRANSITION = 4,    // Page transitions and animations
  PERF_BACKGROUND_SYNC = 5,  // Background data synchronization
  PERF_SYSTEM_STRESS = 6     // High CPU/memory utilization
};

// Smart Network enums
enum NetworkCommandPriority {
  PRIORITY_CRITICAL = 0,    // Immediate execution (power toggle, emergency)
  PRIORITY_HIGH = 1,        // User interaction (brightness, preset changes)
  PRIORITY_NORMAL = 2,      // Regular updates (sync, status)
  PRIORITY_LOW = 3,         // Background tasks (telemetry)
  PRIORITY_BATCH = 4        // Can be batched with other commands
};

enum BatchingStrategy {
  BATCH_NONE = 0,           // Cannot be batched
  BATCH_MERGE = 1,          // Can merge with similar commands
  BATCH_SEQUENCE = 2,       // Can be sequenced with others
  BATCH_CONSOLIDATE = 3     // Can consolidate redundant commands
};

// WebSocket types (from WebSockets library)
#include <WebSocketsClient.h>

// Contextual Preloader structs
typedef struct {
  int8_t fromPage;               // Source page (-1 for any page)
  int8_t toPage;                 // Target page
  uint32_t frequency;            // How often this pattern occurs
  uint32_t totalTransitionTime;  // Total time for all transitions
  uint32_t avgTransitionTime;    // Average transition time
  uint32_t lastSeen;             // When pattern was last observed
  float confidence;              // Pattern confidence (0.0-1.0)
  bool active;                   // Whether pattern is being used for predictions
  uint8_t timeOfDay;             // Hour when pattern typically occurs
  uint8_t sequenceLength;        // Length of navigation sequence
} NavigationPattern;

typedef struct {
  char filename[32];             // Asset filename
  uint8_t* data;                 // Asset data in memory
  uint32_t size;                 // Asset size in bytes
  int8_t associatedPage;         // Which page uses this asset
  uint32_t lastAccessed;         // Last access time
  uint32_t accessCount;          // Number of times accessed
  float priority;                // Preload priority (0.0-1.0)
  bool persistent;               // Always keep in memory
  bool preloaded;                // Currently preloaded
  uint16_t width, height;        // Image dimensions (if applicable)
} AssetCacheEntry;

// Smart Network structs
typedef struct {
  uint8_t commandType;           // Command type (preset, brightness, etc.)
  NetworkCommandPriority priority;
  BatchingStrategy batchStrategy;
  uint32_t timestamp;            // When command was created
  uint32_t deadline;             // Latest acceptable execution time
  uint8_t retryCount;            // Number of retry attempts
  uint32_t lastRetry;            // Last retry timestamp
  uint16_t estimatedLatency;     // Estimated execution time
  char payload[128];             // JSON command payload
  char endpoint[32];             // REST endpoint or WebSocket target
  bool canMerge;                 // Can merge with similar commands
  bool timedOut;                 // Command has timed out
  uint32_t originalValue;        // Original value for deduplication
} SmartNetworkCommand;

typedef struct {
  SmartNetworkCommand commands[8]; // MAX_BATCH_COMMANDS
  uint8_t commandCount;
  uint32_t batchStartTime;
  uint32_t estimatedBatchTime;
  bool readyForExecution;
  char consolidatedPayload[512]; // Consolidated JSON payload
} CommandBatch;

// Forward declarations to prevent Arduino IDE auto-prototype issues
struct IconBtn;
struct BrightnessSlider;

// Forward declarations for all manager functions
// Power Manager
void PowerManager_init();
void PowerManager_update();
void PowerManager_shutdown();
void PowerManager_enterDeepSleep();
void PowerManager_restart();
bool PowerManager_isBatteryEnabled();
int PowerManager_getBatteryVoltage();
int PowerManager_getBatteryPercent();
bool PowerManager_isExternalPower();

// Persistence Manager - ENHANCED: Non-volatile storage for settings
bool PersistenceManager_init();
void PersistenceManager_cleanup();
bool PersistenceManager_saveWLEDInstance(uint8_t instance);
uint8_t PersistenceManager_loadWLEDInstance();
bool PersistenceManager_onWLEDInstanceChanged(uint8_t newInstance);
bool PersistenceManager_isInitialized();
void PersistenceManager_printStorageInfo();

// Network Task Manager - PHASE 2B: Dual-Core Separation
bool NetworkTask_init();
bool NetworkTask_queuePreset(uint8_t preset);
bool NetworkTask_queueQuickLoad(uint8_t slot);
bool NetworkTask_queueBrightness(uint8_t brightness);
bool NetworkTask_queuePowerToggle();
bool NetworkTask_queuePresetCycle(bool next);
bool NetworkTask_queuePaletteCycle(bool next);
// ENHANCED: Fixed Quick Launch Preset Functions - Expanded to 6 slots
bool NetworkTask_queueFixedQuickLaunch1();
bool NetworkTask_queueFixedQuickLaunch2();
bool NetworkTask_queueFixedQuickLaunch3();
bool NetworkTask_queueFixedQuickLaunch4();
bool NetworkTask_queueFixedQuickLaunch5();
bool NetworkTask_queueFixedQuickLaunch6();
bool NetworkTask_queueShufflePreset();
bool NetworkTask_queueFullWhitePreset();
bool NetworkTask_queueMusicPreset();
bool NetworkTask_requestSync();
bool NetworkTask_requestWiFiReconnect();
bool NetworkTask_isInitialized();
uint8_t NetworkTask_getQueueDepth();
void NetworkTask_printStats();
void NetworkTask_cleanup();

// Frequency Manager - PHASE 3: Dynamic CPU Scaling
void FreqManager_init();
void FreqManager_update();
void FreqManager_notifyTouchActivity();
void FreqManager_notifyNetworkActivity(); 
void FreqManager_notifyDisplayActivity();
void FreqManager_notifyGeneralActivity();
uint32_t FreqManager_getCurrentFrequencyMHz();
const char* FreqManager_getModeString();
void FreqManager_printStats();
uint32_t FreqManager_estimatePowerSavingsMW();

// Memory Manager - PHASE 3: Advanced Memory Optimization
void MemoryManager_init();
void MemoryManager_update();
void* MemoryManager_smartAlloc(size_t size);
void MemoryManager_smartFree(void* ptr, size_t size);
void MemoryManager_printStats();
bool MemoryManager_isHealthy();
void MemoryManager_forceGarbageCollection();

// WebSocket Client - PHASE 3: Low-Latency WLED Communication
void WebSocketClient_init();
void WebSocketClient_update();
bool WebSocketClient_sendPreset(uint8_t preset);
bool WebSocketClient_sendBrightness(uint8_t brightness);
bool WebSocketClient_sendPowerToggle();
bool WebSocketClient_isConnected();
void WebSocketClient_printStats();
void WebSocketClient_cleanup();

// Performance Profiler - PHASE 3: Real-Time Performance Analysis
void Profiler_init();
void Profiler_update();
void Profiler_printDetailedReport();
void Profiler_printSummary();
bool Profiler_isEnabled();

// PHASE 4: Advanced Intelligence & Professional UI Systems
// Animation Manager - Smooth UI transitions and animations
void AnimationManager_init();
void AnimationManager_update();
bool AnimationManager_startPageTransition(int sourcePage, int targetPage, int direction);
uint8_t AnimationManager_startButtonBounce(void* button);
bool AnimationManager_isTransitionActive();
bool AnimationManager_isAnyAnimationActive();
void AnimationManager_printStats();

// Intelligent Power Manager - Advanced power states with usage learning
void IPower_init();
void IPower_update();
IntelligentPowerState IPower_getCurrentState();
bool IPower_isAdaptivePowerEnabled();
void IPower_notifyActivity();
void IPower_printStats();
bool IPower_hasLearnedPattern();

// Predictive Performance Scaling - AI-driven performance optimization
void PScale_init();
void PScale_update();
void PScale_notifyEvent(uint8_t eventType, uint16_t duration, uint8_t intensity);
bool PScale_isAdaptiveScalingEnabled();
void PScale_printStats();
float PScale_getOverallAccuracy();

// Differential Rendering Engine - Optimized display updates
void DRender_init();
void DRender_update();
uint8_t DRender_createRegion(int16_t x, int16_t y, int16_t w, int16_t h, bool persistent);
void DRender_markRegionDirty(uint8_t regionIndex);
void DRender_markAreaDirty(int16_t x, int16_t y, int16_t w, int16_t h);
bool DRender_isEnabled();
void DRender_printStats();

// Smart Network Batching - Intelligent network optimization
void SNet_init();
void SNet_update();
bool SNet_queueCommand(uint8_t commandType, uint8_t value);
bool SNet_queuePriorityCommand(uint8_t commandType, uint8_t value, uint8_t priority);
void SNet_printStats();
bool SNet_isNetworkCongested();

// Contextual Asset Preloader - Navigation pattern learning and asset preloading
void CPreload_init();
void CPreload_update();
void CPreload_notifyPageChange(int8_t fromPage, int8_t toPage);
bool CPreload_isAssetCached(const char* filename);
uint8_t* CPreload_getAsset(const char* filename, uint32_t* size);
void CPreload_printStats();
float CPreload_getPredictionAccuracy();

// Display Manager - OPTIMIZED
void DisplayManager_init();
void DisplayManager_postInit();
void DisplayManager_update();
void DisplayManager_updateActivity();
bool DisplayManager_isScreenOn();
Adafruit_ST7789& DisplayManager_getTFT();
void DisplayManager_drawBatteryStatus();
void DisplayManager_clearScreen();
void DisplayManager_printPerformanceStats();
uint32_t DisplayManager_getLastRenderTime();
void DisplayManager_forceUpdate();
void DisplayManager_drawTextCentered(int16_t x, int16_t y, int16_t w, int16_t h,
                                     const char* text, uint16_t fg, uint16_t bg, 
                                     uint8_t size);
void DisplayManager_drawButtonRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                   uint16_t fg, uint16_t bg);
void DisplayManager_resetPerformanceCounters();

// WiFi Manager - OPTIMIZED  
void WiFiManager_connect();
bool WiFiManager_isConnected();
int WiFiManager_getRSSI();
void WiFiManager_update();
void WiFiManager_printStats();
bool WiFiManager_isHealthy();
uint32_t WiFiManager_getDisconnectCount();

// SD Manager - OPTIMIZED + PHASE 2A
void SDManager_init();
bool SDManager_isAvailable();
bool SDManager_drawBMPFromSD(const char* path, int16_t x, int16_t y);
void SDManager_update();
void SDManager_printCacheStats();
void SDManager_checkHeapHealth();
size_t SDManager_getCacheMemoryUsage();
float SDManager_getCacheHitRate();
// PHASE 2A: Asset preloading functions
void SDManager_preloadAllIcons();
bool SDManager_forceLoadIconToCache(const char* path);
bool SDManager_areAssetsPreloaded();

// WLED Client - OPTIMIZED
bool WLEDClient_sendPreset(uint8_t preset);
bool WLEDClient_sendQuickLoad(uint8_t index);
bool WLEDClient_sendBrightness(uint8_t bri);
bool WLEDClient_sendPowerToggle();
bool WLEDClient_sendPresetCycle(bool next);
bool WLEDClient_sendPaletteCycle(bool next);
int WLEDClient_fetchBrightness();
bool WLEDClient_initQuickLoads();
uint8_t WLEDClient_getQuickLoadPreset(uint8_t index);
template<typename TDoc> bool WLEDClient_fetchWledState(TDoc& doc);
void WLEDClient_forceResetBackoff();
void WLEDClient_forceResetHTTPState();
void WLEDClient_periodicSync();
bool WLEDClient_isHealthy();
uint8_t WLEDClient_getFailureCount();
bool WLEDClient_testConnection();
bool WLEDClient_fetchEffectName(int effectId, char* buffer, size_t bufferSize);
bool WLEDClient_fetchPaletteName(int paletteId, char* buffer, size_t bufferSize);
bool WLEDClient_fetchPresetName(int presetId, char* buffer, size_t bufferSize);
bool WLEDClient_detectPlaylist(int currentPresetId, char* playlistNameBuffer, size_t bufferSize, int* playlistId);
void WLEDClient_cleanup();
void WLEDClient_debugTest(); // DEBUG function
void WLEDClient_fetchFriendlyNames();
// NON-BLOCKING queue API
bool WLEDClient_queuePreset(uint8_t preset);
bool WLEDClient_queueQuickLoad(uint8_t slot);
bool WLEDClient_queueBrightness(uint8_t brightness);
bool WLEDClient_queuePowerToggle();
uint8_t WLEDClient_getQueueCount();
// POWER MANAGEMENT API
bool WLEDClient_getPowerState();
bool WLEDClient_setPowerState(bool on);

// Touch Manager - OPTIMIZED + PHASE 3 Advanced Gestures
void TouchManager_init();
void TouchManager_update();
bool TouchManager_isHealthy();
uint8_t TouchManager_getBadReadingCount();
void TouchManager_forceReinit();
bool TouchManager_isLongPressActive();
bool TouchManager_isDoubleTapDetected(); 
uint8_t TouchManager_getCurrentPressureLevel();
uint32_t TouchManager_getCurrentPressDuration();
void TouchManager_printGestureStats();

// Twist Manager (Qwiic Twist Rotary Encoder) - ENHANCED
void TwistManager_init();
void TwistManager_update();
bool TwistManager_isAvailable();
int TwistManager_getCurrentBrightness();
void TwistManager_setBrightness(int brightness);
void TwistManager_syncWithWLED();
void TwistManager_onProgramChange();
bool TwistManager_isInPowerSavingMode();
void TwistManager_fetchWLEDPalette();
void TwistManager_showHTTPFeedback(bool success);
void TwistManager_updateHTTPFeedback();
void TwistManager_schedulePalettePreview(uint32_t delayMs);

// UI Manager - ENHANCED
void UIManager_init();
void UIManager_postInit();
void UIManager_update();
void UIManager_handleTouch(int16_t x, int16_t y, bool isPress, bool isRelease);
void UIManager_paintPage();
void UIManager_navToPage(int newPage);
int UIManager_getCurrentPage();
int UIManager_getCurrentPhysicalPage();
bool UIManager_isBrightnessPageEnabled();
void UIManager_forceRepaint();
bool UIManager_needsRepaint();
#if ENABLE_NOW_PLAYING_PAGE
void UIManager_drawNowPlayingPage();
void UIManager_drawWLEDStateInfo(JsonDocument& doc);
bool UIManager_drawPaletteRepresentativeColors(int paletteId, int startX, int startY);
const char* UIManager_getEffectName(int effectId);
const char* UIManager_getPaletteName(int paletteId);
// Combo box functions
int UIManager_getAvailablePlaylists(int* playlistIds, char playlistNames[][64], int maxPlaylists);
int UIManager_getCurrentPlaylistId();
void UIManager_activatePlaylist(int playlistId);
int UIManager_getAvailablePresets(int* presetIds, char presetNames[][64], int maxPresets);
int UIManager_getCurrentPresetId();
void UIManager_activatePreset(int presetId);
int UIManager_getCurrentEffectId();
int UIManager_getTotalEffectsCount();
void UIManager_changeEffect(int effectId);
int UIManager_getCurrentPaletteId();
int UIManager_getTotalPalettesCount();
void UIManager_changePalette(int paletteId);
#endif
// Experimental font-based UI
void UIManager_drawTextButton(const IconBtn& btn, bool pressed, const char* iconText);

// OPTIMIZED: Performance monitoring
static uint32_t MainLoop_loopCount = 0;
static uint32_t MainLoop_lastPerformanceReport = 0;
static uint32_t MainLoop_maxLoopTime = 0;
static uint32_t MainLoop_lastHeapCheck = 0;

// OPTIMIZED: System health monitoring
void MainLoop_checkSystemHealth() {
  uint32_t now = millis();
  
  // Check every 30 seconds
  if (now - MainLoop_lastHeapCheck < 30000) return;
  MainLoop_lastHeapCheck = now;
  
  size_t freeHeap = ESP.getFreeHeap();
  size_t minFreeHeap = ESP.getMinFreeHeap();
  
  if (freeHeap < 50000) { // 50KB threshold - using constant instead of macro
    Serial.printf("[SYSTEM] LOW HEAP WARNING: %d bytes free (min was %d)\n", 
                  freeHeap, minFreeHeap);
    
    // Trigger cache cleanup
    SDManager_checkHeapHealth();
    
    // PHASE 3: Force memory garbage collection
    MemoryManager_forceGarbageCollection();
    
    // Force garbage collection
    WiFiManager_isConnected();
  }
  
  // Check for system instability indicators
  bool wifiHealthy = WiFiManager_isHealthy();
  bool touchHealthy = TouchManager_isHealthy();
  bool wledHealthy = WLEDClient_isHealthy();
  
  if (!wifiHealthy || !touchHealthy || !wledHealthy) {
    Serial.printf("[SYSTEM] Health check: WiFi=%s Touch=%s WLED=%s\n",
                  wifiHealthy ? "OK" : "FAIL",
                  touchHealthy ? "OK" : "FAIL", 
                  wledHealthy ? "OK" : "FAIL");
  }
}

// OPTIMIZED: Performance reporting
void MainLoop_printPerformanceReport() {
  uint32_t now = millis();
  
  if (now - MainLoop_lastPerformanceReport < 60000) return; // Every minute
  MainLoop_lastPerformanceReport = now;
  
  uint32_t uptime = now / 1000;
  uint32_t avgLoopsPerSec = MainLoop_loopCount / max(1UL, (now / 1000));
  
  Serial.println("\n=== PERFORMANCE REPORT ===");
  Serial.printf("Uptime: %ds, Loops/sec: %d (max loop: %dμs)\n", 
                uptime, avgLoopsPerSec, MainLoop_maxLoopTime);
  
  // Display stats
  DisplayManager_printPerformanceStats();
  
  // WiFi stats
  WiFiManager_printStats();
  
  // PHASE 2B: Network task stats
  if (NetworkTask_isInitialized()) {
    NetworkTask_printStats();
  }
  
  // PHASE 3: Frequency manager stats
  FreqManager_printStats();
  
  // PHASE 3: Memory manager stats
  MemoryManager_printStats();
  
  // PHASE 3: WebSocket client stats
  WebSocketClient_printStats();
  
  // PHASE 3: Performance profiler summary
  if (Profiler_isEnabled()) {
    Profiler_printSummary();
  }
  
  // PHASE 4: Advanced intelligence and professional UI system stats
  Serial.println("\n--- PHASE 4 ADVANCED SYSTEMS ---");
  
  // Animation manager stats
  AnimationManager_printStats();
  
  // Intelligent power manager stats
  IPower_printStats();
  
  // Predictive performance scaling stats
  PScale_printStats();
  
  // Differential rendering engine stats
  DRender_printStats();
  
  // Smart network batching stats
  SNet_printStats();
  
  // Contextual asset preloader stats
  CPreload_printStats();
  
  // SD cache stats
  SDManager_printCacheStats();
  
  // WLED health
  Serial.printf("WLED: %s, Failures: %d\n", 
                WLEDClient_isHealthy() ? "Healthy" : "Unhealthy",
                WLEDClient_getFailureCount());
  
  // Touch health + PHASE 3 gesture stats
  Serial.printf("Touch: %s, Bad reads: %d\n",
                TouchManager_isHealthy() ? "Healthy" : "Unhealthy", 
                TouchManager_getBadReadingCount());
  
  #if ENABLE_ADVANCED_GESTURES
  TouchManager_printGestureStats();
  #endif
  
  // Memory stats + PHASE 2A status
  Serial.printf("Heap: %d free, %d min, Cache: %.1f%% hit, %d bytes%s\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                SDManager_getCacheHitRate() * 100.0f,
                SDManager_getCacheMemoryUsage(),
                SDManager_areAssetsPreloaded() ? " [PRELOADED]" : "");
  
  // ENHANCED: Power management status
  if (TwistManager_isAvailable()) {
    Serial.printf("Twist: %s mode, brightness: %d\n",
                  TwistManager_isInPowerSavingMode() ? "POWER_SAVING" : "NORMAL",
                  TwistManager_getCurrentBrightness());
  }
  
  // ENHANCED: UI configuration status
  Serial.printf("UI: Page %d/%d (physical: %d), Brightness page: %s\n",
                UIManager_getCurrentPage() + 1, TOTAL_UI_PAGES,
                UIManager_getCurrentPhysicalPage(),
                UIManager_isBrightnessPageEnabled() ? "ENABLED" : "DISABLED");
  
  Serial.println("=========================\n");
  
  // Reset counters for next period
  MainLoop_maxLoopTime = 0;
  DisplayManager_resetPerformanceCounters();
}

void setup() {
  Serial.begin(115200);
  delay(30);
  Serial.println("\n[BOOT] Starting ENHANCED WLED Remote...");
  
  uint32_t setupStart = millis();

  // CRITICAL: Ensure WLED instance is valid before any operations
  ensureValidWLEDInstance();
  Serial.printf("[CONFIG] WLED instance validated: %d/%d\n", CURRENT_WLED_INSTANCE, WLED_INSTANCE_COUNT);

  // ENHANCED: Display configuration info
  Serial.printf("[CONFIG] Brightness page: %s\n", ENABLE_BRIGHTNESS_PAGE ? "ENABLED" : "DISABLED");
  Serial.printf("[CONFIG] WLED selection page: %s\n", ENABLE_WLED_SELECTION_PAGE ? "ENABLED" : "DISABLED");
  Serial.printf("[CONFIG] Total pages: %d\n", TOTAL_UI_PAGES);

  // ENHANCED: Initialize persistence manager first to load saved settings
  Serial.println("[STEP] Initializing persistence manager...");
  if (!PersistenceManager_init()) {
    Serial.println("[STEP] Persistence manager failed - using defaults");
  }
  
  // Re-validate WLED instance after persistence load
  ensureValidWLEDInstance();

  // Initialize power management
  Serial.println("[STEP] Initializing power management...");
  PowerManager_init();

  // Initialize display
  Serial.println("[STEP] Initializing display...");
  DisplayManager_init();

  // Initialize SD card
  Serial.println("[STEP] Initializing SD card...");
  SDManager_init();

  // Initialize UI (depends on display and SD)
  Serial.println("[STEP] Initializing UI...");
  UIManager_init();
  
  Serial.println("[STEP] UI painted");

  // Initialize touch controller
  Serial.println("[STEP] Initializing touch controller...");
  TouchManager_init();

  // Initialize Qwiic Twist rotary encoder
  Serial.println("[STEP] Initializing Qwiic Twist...");
  TwistManager_init();

  // PHASE 3: Initialize frequency manager
  Serial.println("[STEP] Initializing frequency manager...");
  FreqManager_init();
  
  // CRITICAL FIX: Now that frequency manager is ready, complete display setup
  Serial.println("[STEP] Completing display initialization...");
  DisplayManager_postInit();

  // PHASE 3: Initialize memory manager (only if optimization enabled)
  #if ENABLE_MEMORY_OPTIMIZATION
  Serial.println("[STEP] Initializing memory manager...");
  MemoryManager_init();
  #endif

  // PHASE 3: Initialize WebSocket client (only if enabled)
  #if ENABLE_WEBSOCKET_WLED
  Serial.println("[STEP] Initializing WebSocket client...");
  WebSocketClient_init();
  #endif

  // PHASE 3: Initialize performance profiler (only if telemetry enabled)
  #if ENABLE_SYSTEM_TELEMETRY
  Serial.println("[STEP] Initializing performance profiler...");
  Profiler_init();
  #endif

  // PHASE 4: Initialize advanced intelligence and professional UI systems
  Serial.println("[STEP] Initializing Phase 4 advanced systems...");
  
  // Initialize animation manager for smooth UI transitions (only if enabled)
  #if ENABLE_SMOOTH_ANIMATIONS
  Serial.println("[STEP] Initializing animation manager...");
  AnimationManager_init();
  #endif
  
  // Initialize intelligent power management (only if enabled)
  #if ENABLE_INTELLIGENT_POWER
  Serial.println("[STEP] Initializing intelligent power manager...");
  IPower_init();
  #endif
  
  // Initialize predictive performance scaling (only if enabled)
  #if ENABLE_PREDICTIVE_SCALING
  Serial.println("[STEP] Initializing predictive scaling...");
  PScale_init();
  #endif
  
  // Initialize differential rendering engine (only if enabled)
  #if ENABLE_DIFFERENTIAL_RENDERING
  Serial.println("[STEP] Initializing differential renderer...");
  DRender_init();
  #endif
  
  // Initialize smart network and contextual preloader (only if caching enabled)
  #if ENABLE_SMART_CACHING
  Serial.println("[STEP] Initializing smart network batching...");
  SNet_init();
  Serial.println("[STEP] Initializing contextual preloader...");
  CPreload_init();
  #endif

  // PHASE 2B: Initialize network task for dual-core operation
  Serial.println("[STEP] Initializing network task...");
  if (NetworkTask_init()) {
    Serial.println("[STEP] Network task initialized - operations moved to core 0");
  } else {
    Serial.println("[STEP] Network task failed - falling back to single-core mode");
    // Fallback to original initialization
    WiFiManager_connect();
    if (WiFiManager_isConnected()) {
      WLEDClient_initQuickLoads();
      WLEDClient_testConnection();
      WLEDClient_fetchFriendlyNames(); // Fetch WLED instance names
    }
  }

  uint32_t setupTime = millis() - setupStart;
  Serial.printf("[BOOT] Setup complete in %dms\n", setupTime);
  
  // CRITICAL FIX: Now that system is fully initialized, paint the UI
  Serial.println("[BOOT] Painting initial UI...");
  UIManager_postInit();
  
  Serial.println("[BOOT] Starting enhanced main loop...");
  Serial.printf("[BOOT] Features: Brightness page %s, Power management ENABLED\n", 
                ENABLE_BRIGHTNESS_PAGE ? "ENABLED" : "DISABLED");
  
  MainLoop_lastPerformanceReport = millis();
}

void loop() {
  uint32_t loopStart = micros();
  
  // OPTIMIZED: Separate update frequencies for different subsystems
  
  // HIGH FREQUENCY: Touch input (1ms polling)
  TouchManager_update();
  FreqManager_notifyTouchActivity(); // PHASE 3: Notify frequency manager of touch activity
  
  // MEDIUM FREQUENCY: Twist encoder (16ms) - Enhanced with power management
  TwistManager_update();
  
  // MEDIUM FREQUENCY: Display updates (33ms)
  DisplayManager_update();
  FreqManager_notifyDisplayActivity(); // PHASE 3: Notify frequency manager of display activity
  
  // PHASE 3: Dynamic frequency scaling
  FreqManager_update();
  
  // PHASE 3: Memory management (only if optimization enabled)
  #if ENABLE_MEMORY_OPTIMIZATION
  MemoryManager_update();
  #endif
  
  // PHASE 3: Performance profiler updates (only if telemetry enabled)
  #if ENABLE_SYSTEM_TELEMETRY
  Profiler_update();
  #endif
  
  // PHASE 4: Advanced intelligence and professional UI system updates
  // Animation manager for smooth transitions (only if enabled)
  #if ENABLE_SMOOTH_ANIMATIONS
  AnimationManager_update();
  #endif
  
  // Intelligent power management with learning (only if enabled)
  #if ENABLE_INTELLIGENT_POWER
  IPower_update();
  #endif
  
  // Predictive performance scaling (only if enabled)
  #if ENABLE_PREDICTIVE_SCALING
  PScale_update();
  #endif
  
  // Differential rendering engine (only if enabled)
  #if ENABLE_DIFFERENTIAL_RENDERING
  DRender_update();
  #endif
  
  // Smart network batching and contextual preloader (only if caching enabled)
  #if ENABLE_SMART_CACHING
  SNet_update();
  CPreload_update();
  #endif
  
  // PHASE 3: WebSocket client updates (only if enabled)
  #if ENABLE_WEBSOCKET_WLED && !ENABLE_DUAL_CORE_SEPARATION
  // Only run WebSocket on main core if dual-core is disabled
  WebSocketClient_update();
  #endif
  
  // PHASE 2B: Network operations now handled by dedicated task on core 0
  // Only run these if dual-core separation is disabled
  #if !ENABLE_DUAL_CORE_SEPARATION
  // LOW FREQUENCY: WiFi management (10s)
  WiFiManager_update();
  
  // LOW FREQUENCY: WLED periodic sync (30s)
  WLEDClient_periodicSync();
  #endif
  
  // LOW FREQUENCY: SD cache maintenance (10s) - stays on UI core
  SDManager_update();
  
  // VERY LOW FREQUENCY: System health (30s)
  MainLoop_checkSystemHealth();
  
  // UI updates (event-driven through touch handling)
  UIManager_update();
  
  // Power management (battery monitoring)
  PowerManager_update();

  // OPTIMIZED: Performance tracking
  uint32_t loopTime = micros() - loopStart;
  if (loopTime > MainLoop_maxLoopTime) {
    MainLoop_maxLoopTime = loopTime;
  }
  
  MainLoop_loopCount++;

  // OPTIMIZED: Performance reporting
  MainLoop_printPerformanceReport();

  // OPTIMIZED: Heartbeat logging (reduced frequency) with enhanced info
  static uint32_t lastHB = 0; 
  if (millis() - lastHB > 10000) { // Every 10 seconds instead of 2
    lastHB = millis();
    uint32_t loopsPerSec = MainLoop_loopCount / max(1UL, (millis() / 1000));
    
    Serial.printf("[HB] up=%lus loops/s=%lu max_loop=%luμs WiFi=%s(%ddBm) page=%d/%d screen=%s QL=%u,%u,%u",
                  millis()/1000, 
                  loopsPerSec,
                  MainLoop_maxLoopTime,
                  WiFiManager_isConnected() ? "up" : "down", 
                  WiFi.RSSI(), 
                  UIManager_getCurrentPage() + 1,
                  TOTAL_UI_PAGES,
                  DisplayManager_isScreenOn() ? "on" : "off",
                  WLEDClient_getQuickLoadPreset(0), 
                  WLEDClient_getQuickLoadPreset(1), 
                  WLEDClient_getQuickLoadPreset(2));
    
    if (TwistManager_isAvailable()) {
      Serial.printf(" twist=%s,bri=%d", 
                    TwistManager_isInPowerSavingMode() ? "sleep" : "norm",
                    TwistManager_getCurrentBrightness());
    }
    
    uint8_t queueCount = NetworkTask_isInitialized() ? 
                       NetworkTask_getQueueDepth() : WLEDClient_getQueueCount();
    Serial.printf(" heap=%d cache=%.0f%% queue=%d freq=%dMHz core=%s\n", 
                  ESP.getFreeHeap(),
                  SDManager_getCacheHitRate() * 100.0f,
                  queueCount,
                  FreqManager_getCurrentFrequencyMHz(),
                  NetworkTask_isInitialized() ? "dual" : "single");
  }
  
  // OPTIMIZED: Minimal delay with smarter yielding
  static uint8_t yieldCounter = 0;
  if (++yieldCounter >= 100) { // Yield every 100 loops instead of every loop
    yield();
    yieldCounter = 0;
  }
  
  // Ultra-minimal delay
  delayMicroseconds(500); // 0.5ms instead of 1ms
}