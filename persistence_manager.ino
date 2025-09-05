// ─────────────────────────────────────────────────────────────
// persistence_manager.ino - Non-volatile Storage for Settings
// Uses ESP32 Preferences library for persistent storage
// ─────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <Preferences.h>

// Module state
static Preferences PersistenceManager_prefs;
static bool PersistenceManager_initialized = false;

// Storage namespace and keys
static const char* PERSISTENCE_NAMESPACE = "wled_remote";
static const char* KEY_WLED_INSTANCE = "wled_inst";

// External reference to the config variables
extern uint8_t CURRENT_WLED_INSTANCE;
extern const uint8_t WLED_INSTANCE_COUNT;

// Forward declarations (will be implemented below)
bool PersistenceManager_init();
void PersistenceManager_cleanup();
bool PersistenceManager_saveWLEDInstance(uint8_t instance);
uint8_t PersistenceManager_loadWLEDInstance();
bool PersistenceManager_onWLEDInstanceChanged(uint8_t newInstance);
bool PersistenceManager_isInitialized();
void PersistenceManager_printStorageInfo();

// ───── Public API ─────

bool PersistenceManager_init() {
  Serial.println("[PERSIST] Initializing persistence manager...");
  
  // Initialize preferences with namespace
  if (!PersistenceManager_prefs.begin(PERSISTENCE_NAMESPACE, false)) {
    Serial.println("[PERSIST] Failed to initialize preferences");
    return false;
  }
  
  PersistenceManager_initialized = true;
  Serial.println("[PERSIST] Persistence manager initialized");
  
  // Load saved WLED instance on startup
  uint8_t savedInstance = PersistenceManager_loadWLEDInstance();
  if (savedInstance < WLED_INSTANCE_COUNT) {
    CURRENT_WLED_INSTANCE = savedInstance;
    Serial.printf("[PERSIST] Restored WLED instance: %d\n", savedInstance);
  } else {
    Serial.printf("[PERSIST] Invalid saved instance %d, using default 0\n", savedInstance);
    CURRENT_WLED_INSTANCE = 0;
  }
  
  return true;
}

void PersistenceManager_cleanup() {
  if (PersistenceManager_initialized) {
    PersistenceManager_prefs.end();
    PersistenceManager_initialized = false;
    Serial.println("[PERSIST] Persistence manager cleaned up");
  }
}

bool PersistenceManager_saveWLEDInstance(uint8_t instance) {
  if (!PersistenceManager_initialized) {
    Serial.println("[PERSIST] Not initialized - cannot save");
    return false;
  }
  
  if (instance >= WLED_INSTANCE_COUNT) {
    Serial.printf("[PERSIST] Invalid instance %d (max %d)\n", instance, WLED_INSTANCE_COUNT - 1);
    return false;
  }
  
  size_t bytesWritten = PersistenceManager_prefs.putUChar(KEY_WLED_INSTANCE, instance);
  if (bytesWritten > 0) {
    Serial.printf("[PERSIST] Saved WLED instance: %d\n", instance);
    return true;
  } else {
    Serial.printf("[PERSIST] Failed to save WLED instance: %d\n", instance);
    return false;
  }
}

uint8_t PersistenceManager_loadWLEDInstance() {
  if (!PersistenceManager_initialized) {
    Serial.println("[PERSIST] Not initialized - using default instance 0");
    return 0;
  }
  
  // Load with default value of 0 if key doesn't exist
  uint8_t instance = PersistenceManager_prefs.getUChar(KEY_WLED_INSTANCE, 0);
  
  if (instance < WLED_INSTANCE_COUNT) {
    Serial.printf("[PERSIST] Loaded WLED instance: %d\n", instance);
    return instance;
  } else {
    Serial.printf("[PERSIST] Loaded invalid instance %d, using default 0\n", instance);
    return 0;
  }
}

bool PersistenceManager_isInitialized() {
  return PersistenceManager_initialized;
}

// ───── Convenience Functions ─────

// Call this whenever the user changes the WLED instance
bool PersistenceManager_onWLEDInstanceChanged(uint8_t newInstance) {
  if (newInstance != CURRENT_WLED_INSTANCE) {
    CURRENT_WLED_INSTANCE = newInstance;
    Serial.printf("[PERSIST] WLED instance changed to %u - refreshing quick loads\n", newInstance);
    
    // CRITICAL FIX: Refresh quick loads for the new instance
    if (WiFiManager_isConnected()) {
      WLEDClient_initQuickLoads();
    }
    
    return PersistenceManager_saveWLEDInstance(newInstance);
  }
  return true; // No change needed
}

// Get storage info for debugging
void PersistenceManager_printStorageInfo() {
  if (!PersistenceManager_initialized) {
    Serial.println("[PERSIST] Not initialized");
    return;
  }
  
  Serial.println("\n=== PERSISTENCE STORAGE INFO ===");
  Serial.printf("Namespace: %s\n", PERSISTENCE_NAMESPACE);
  Serial.printf("Current WLED instance: %d\n", CURRENT_WLED_INSTANCE);
  Serial.printf("Stored WLED instance: %d\n", PersistenceManager_loadWLEDInstance());
  Serial.println("================================\n");
}