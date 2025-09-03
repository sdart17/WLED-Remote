// ─────────────────────────────────────────────────────────────
// Network Task Manager - PHASE 2B: Dual-Core FreeRTOS Implementation
// Moves all network operations (WiFi, HTTP, WLED) to Core 0, keeping UI on Core 1
// ─────────────────────────────────────────────────────────────

#if ENABLE_DUAL_CORE_SEPARATION

// ───── FreeRTOS Task Handle ─────
static TaskHandle_t NetworkTask_handle = nullptr;
static bool NetworkTask_initialized = false;

// ───── Network Command Queue ─────
struct NetworkCommand {
  uint8_t type;        // Command type (see enum below)
  uint8_t value;       // Command value
  uint32_t timestamp;  // When queued
};

// NetworkCommandType enum is now defined in the main .ino file

static QueueHandle_t NetworkTask_commandQueue = nullptr;
static SemaphoreHandle_t NetworkTask_mutex = nullptr;

// ───── Network Task Statistics ─────
static uint32_t NetworkTask_commandsProcessed = 0;
static uint32_t NetworkTask_commandsDropped = 0;
static uint32_t NetworkTask_maxQueueDepth = 0;
static uint32_t NetworkTask_avgProcessingTime = 0;

// Forward declarations for functions we'll call from network task
bool WLEDClient_sendPreset(uint8_t preset);
bool WLEDClient_sendQuickLoad(uint8_t slot);
bool WLEDClient_sendBrightness(uint8_t bri);
bool WLEDClient_sendPowerToggle();
bool WLEDClient_sendPresetCycle(bool next);
bool WLEDClient_sendPaletteCycle(bool next);
void WLEDClient_periodicSync();
void WiFiManager_update();
bool TwistManager_isAvailable();
void TwistManager_onProgramChange();
void FreqManager_notifyNetworkActivity();
void WebSocketClient_update();

// ───── Core Network Task Function ─────
void NetworkTask_taskFunction(void* parameter) {
  Serial.printf("[NET_TASK] Starting on core %d\n", xPortGetCoreID());
  
  // Initialize network components on this core
  Serial.println("[NET_TASK] Initializing WiFi...");
  WiFiManager_connect();
  
  Serial.println("[NET_TASK] Initializing WLED client...");
  WLEDClient_initQuickLoads();
  
  Serial.println("[NET_TASK] Fetching WLED instance names...");
  WLEDClient_fetchFriendlyNames();
  
  TickType_t lastWiFiUpdate = 0;
  TickType_t lastPeriodicSync = 0;
  
  while (true) {
    TickType_t taskStartTime = xTaskGetTickCount();
    NetworkCommand cmd;
    
    // Process queued commands with 50ms timeout
    if (xQueueReceive(NetworkTask_commandQueue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
      uint32_t processingStart = millis();
      bool success = false;
      
      // Update queue depth statistics
      UBaseType_t queueDepth = uxQueueMessagesWaiting(NetworkTask_commandQueue);
      if (queueDepth > NetworkTask_maxQueueDepth) {
        NetworkTask_maxQueueDepth = queueDepth;
      }
      
      // Process command based on type
      FreqManager_notifyNetworkActivity(); // PHASE 3: Notify frequency manager
      
      switch (cmd.type) {
        case NET_CMD_PRESET:
          success = WLEDClient_sendPreset(cmd.value);
          if (success && TwistManager_isAvailable()) {
            TwistManager_onProgramChange();
          }
          break;
          
        case NET_CMD_QUICKLOAD:
          success = WLEDClient_sendQuickLoad(cmd.value);
          if (success && TwistManager_isAvailable()) {
            TwistManager_onProgramChange();
          }
          break;
          
        case NET_CMD_BRIGHTNESS:
          success = WLEDClient_sendBrightness(cmd.value);
          break;
          
        case NET_CMD_POWER_TOGGLE:
          success = WLEDClient_sendPowerToggle();
          break;
          
        case NET_CMD_PRESET_CYCLE:
          success = WLEDClient_sendPresetCycle(cmd.value != 0);
          break;
          
        case NET_CMD_PALETTE_CYCLE:
          success = WLEDClient_sendPaletteCycle(cmd.value != 0);
          break;
          
        case NET_CMD_SYNC_STATE:
          WLEDClient_periodicSync();
          success = true;
          break;
          
        case NET_CMD_WIFI_RECONNECT:
          WiFiManager_forceReconnect();
          success = true;
          break;
          
        default:
          Serial.printf("[NET_TASK] Unknown command type: %d\n", cmd.type);
          break;
      }
      
      // Provide visual feedback via encoder LED for HTTP commands
      TwistManager_showHTTPFeedback(success);
      
      uint32_t processingTime = millis() - processingStart;
      NetworkTask_commandsProcessed++;
      
      // Update running average of processing time
      NetworkTask_avgProcessingTime = 
        (NetworkTask_avgProcessingTime * 7 + processingTime) / 8;
      
      Serial.printf("[NET_TASK] Processed cmd %d val=%d result=%s time=%dms queue=%d\n",
                    cmd.type, cmd.value, success ? "OK" : "FAIL", 
                    processingTime, queueDepth);
      
      // Warn if command took too long
      if (processingTime > 1000) {
        Serial.printf("[NET_TASK] WARNING: Command took %dms\n", processingTime);
      }
    }
    
    // Periodic WiFi management (every 10s)
    TickType_t now = xTaskGetTickCount();
    if (now - lastWiFiUpdate > pdMS_TO_TICKS(WIFI_CHECK_INTERVAL_MS)) {
      WiFiManager_update();
      lastWiFiUpdate = now;
    }
    
    // Periodic WLED sync (every 30s)
    if (now - lastPeriodicSync > pdMS_TO_TICKS(WLED_SYNC_INTERVAL_MS)) {
      WLEDClient_periodicSync();
      lastPeriodicSync = now;
    }
    
    // PHASE 3: Update WebSocket client on network core
    WebSocketClient_update();
    
    // Yield to other tasks
    taskYIELD();
  }
}

// ───── Queue Command Helper ─────
bool NetworkTask_queueCommand(NetworkCommandType type, uint8_t value) {
  if (!NetworkTask_initialized || NetworkTask_commandQueue == nullptr) {
    Serial.println("[NET_TASK] Not initialized - command ignored");
    return false;
  }
  
  NetworkCommand cmd = {
    .type = (uint8_t)type,
    .value = value,
    .timestamp = millis()
  };
  
  // Try to queue with short timeout to avoid UI blocking
  if (xQueueSend(NetworkTask_commandQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
    return true;
  } else {
    NetworkTask_commandsDropped++;
    Serial.printf("[NET_TASK] Queue full - dropped cmd %d (total dropped: %d)\n", 
                  type, NetworkTask_commandsDropped);
    return false;
  }
}

// ───── Public API Functions ─────
bool NetworkTask_init() {
  if (NetworkTask_initialized) {
    Serial.println("[NET_TASK] Already initialized");
    return true;
  }
  
  Serial.println("[NET_TASK] Initializing dual-core network separation...");
  
  // Create command queue
  NetworkTask_commandQueue = xQueueCreate(NETWORK_QUEUE_SIZE, sizeof(NetworkCommand));
  if (NetworkTask_commandQueue == nullptr) {
    Serial.println("[NET_TASK] Failed to create command queue");
    return false;
  }
  
  // Create mutex for shared resources
  NetworkTask_mutex = xSemaphoreCreateMutex();
  if (NetworkTask_mutex == nullptr) {
    Serial.println("[NET_TASK] Failed to create mutex");
    vQueueDelete(NetworkTask_commandQueue);
    return false;
  }
  
  // Create network task pinned to core 0
  BaseType_t result = xTaskCreatePinnedToCore(
    NetworkTask_taskFunction,     // Task function
    "NetworkTask",                // Task name
    NETWORK_TASK_STACK_SIZE,      // Stack size
    nullptr,                      // Parameters
    NETWORK_TASK_PRIORITY,        // Priority
    &NetworkTask_handle,          // Task handle
    NETWORK_CORE                  // Pin to core 0
  );
  
  if (result != pdPASS) {
    Serial.printf("[NET_TASK] Failed to create task: %d\n", result);
    vSemaphoreDelete(NetworkTask_mutex);
    vQueueDelete(NetworkTask_commandQueue);
    return false;
  }
  
  NetworkTask_initialized = true;
  Serial.printf("[NET_TASK] Initialized successfully (queue size: %d)\n", NETWORK_QUEUE_SIZE);
  return true;
}

// ───── Command Queue Functions ─────
bool NetworkTask_queuePreset(uint8_t preset) {
  return NetworkTask_queueCommand(NET_CMD_PRESET, preset);
}

bool NetworkTask_queueQuickLoad(uint8_t slot) {
  return NetworkTask_queueCommand(NET_CMD_QUICKLOAD, slot);
}

bool NetworkTask_queueBrightness(uint8_t brightness) {
  return NetworkTask_queueCommand(NET_CMD_BRIGHTNESS, brightness);
}

bool NetworkTask_queuePowerToggle() {
  return NetworkTask_queueCommand(NET_CMD_POWER_TOGGLE, 0);
}

bool NetworkTask_queuePresetCycle(bool next) {
  return NetworkTask_queueCommand(NET_CMD_PRESET_CYCLE, next ? 1 : 0);
}

bool NetworkTask_queuePaletteCycle(bool next) {
  return NetworkTask_queueCommand(NET_CMD_PALETTE_CYCLE, next ? 1 : 0);
}

bool NetworkTask_requestSync() {
  return NetworkTask_queueCommand(NET_CMD_SYNC_STATE, 0);
}

bool NetworkTask_requestWiFiReconnect() {
  return NetworkTask_queueCommand(NET_CMD_WIFI_RECONNECT, 0);
}

// ───── Status and Statistics ─────
bool NetworkTask_isInitialized() {
  return NetworkTask_initialized;
}

uint8_t NetworkTask_getQueueDepth() {
  if (NetworkTask_commandQueue == nullptr) return 0;
  return (uint8_t)uxQueueMessagesWaiting(NetworkTask_commandQueue);
}

uint32_t NetworkTask_getCommandsProcessed() {
  return NetworkTask_commandsProcessed;
}

uint32_t NetworkTask_getCommandsDropped() {
  return NetworkTask_commandsDropped;
}

uint32_t NetworkTask_getMaxQueueDepth() {
  return NetworkTask_maxQueueDepth;
}

uint32_t NetworkTask_getAvgProcessingTime() {
  return NetworkTask_avgProcessingTime;
}

void NetworkTask_printStats() {
  Serial.printf("[NET_TASK] Stats: Processed=%d, Dropped=%d, Queue=%d/%d, MaxQueue=%d, AvgTime=%dms\n",
                NetworkTask_commandsProcessed, NetworkTask_commandsDropped,
                NetworkTask_getQueueDepth(), NETWORK_QUEUE_SIZE,
                NetworkTask_maxQueueDepth, NetworkTask_avgProcessingTime);
  
  if (NetworkTask_handle != nullptr) {
    Serial.printf("[NET_TASK] Task: Core=%d, Priority=%d, Stack=%d\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NetworkTask_handle),
                  NETWORK_TASK_STACK_SIZE);
  }
}

// ───── Cleanup Function ─────
void NetworkTask_cleanup() {
  if (!NetworkTask_initialized) return;
  
  Serial.println("[NET_TASK] Cleaning up...");
  
  if (NetworkTask_handle != nullptr) {
    vTaskDelete(NetworkTask_handle);
    NetworkTask_handle = nullptr;
  }
  
  if (NetworkTask_commandQueue != nullptr) {
    vQueueDelete(NetworkTask_commandQueue);
    NetworkTask_commandQueue = nullptr;
  }
  
  if (NetworkTask_mutex != nullptr) {
    vSemaphoreDelete(NetworkTask_mutex);
    NetworkTask_mutex = nullptr;
  }
  
  NetworkTask_initialized = false;
  Serial.println("[NET_TASK] Cleanup complete");
}

// Missing functions for compatibility
bool NetworkTask_isBurst() { return false; }
bool NetworkTask_hasActivity() { return false; }

#else // ENABLE_DUAL_CORE_SEPARATION is false

// ───── Fallback Functions When Dual-Core is Disabled ─────
bool NetworkTask_init() { return true; }
bool NetworkTask_queuePreset(uint8_t preset) { return WLEDClient_queuePreset(preset); }
bool NetworkTask_queueQuickLoad(uint8_t slot) { return WLEDClient_queueQuickLoad(slot); }
bool NetworkTask_queueBrightness(uint8_t brightness) { return WLEDClient_queueBrightness(brightness); }
bool NetworkTask_queuePowerToggle() { return WLEDClient_queuePowerToggle(); }
bool NetworkTask_queuePresetCycle(bool next) { return WLEDClient_sendPresetCycle(next); }
bool NetworkTask_queuePaletteCycle(bool next) { return WLEDClient_sendPaletteCycle(next); }
bool NetworkTask_requestSync() { WLEDClient_periodicSync(); return true; }
bool NetworkTask_requestWiFiReconnect() { WiFiManager_forceReconnect(); return true; }
bool NetworkTask_isInitialized() { return true; }
uint8_t NetworkTask_getQueueDepth() { return WLEDClient_getQueueCount(); }
uint32_t NetworkTask_getCommandsProcessed() { return 0; }
uint32_t NetworkTask_getCommandsDropped() { return 0; }
uint32_t NetworkTask_getMaxQueueDepth() { return 0; }
uint32_t NetworkTask_getAvgProcessingTime() { return 0; }
void NetworkTask_printStats() { Serial.println("[NET_TASK] Dual-core disabled - using fallback"); }
void NetworkTask_cleanup() {}

// Missing functions for compatibility
bool NetworkTask_isBurst() { return false; }
bool NetworkTask_hasActivity() { return false; }

#endif // ENABLE_DUAL_CORE_SEPARATION