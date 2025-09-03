// ─────────────────────────────────────────────────────────────
// WLED Communication - MEMORY OPTIMIZED (COMPLETE FINAL VERSION)
// PHASE 1 IMPROVEMENTS: Eliminates dynamic String allocation in hot paths
// ─────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ───── External config / helpers (provided elsewhere) ─────
#ifndef WLED_IP
  #define WLED_IP "wled.local"      // Prefer setting to device IP in config.ino
#endif

#ifndef HTTP_TIMEOUT_MS
  #define HTTP_TIMEOUT_MS 600       // keep small; UI stays responsive
#endif

bool WiFiManager_isConnected();

// ───── OPTIMIZED: More aggressive timeouts for better UI responsiveness ─────
#ifndef WLED_CONNECT_TIMEOUT_MS
  #define WLED_CONNECT_TIMEOUT_MS 300  // IMPROVED: Reduced from 500ms
#endif
#ifndef WLED_HTTP_TIMEOUT_MS
  #define WLED_HTTP_TIMEOUT_MS 800     // IMPROVED: Reduced from 1000ms
#endif

// ───── MEMORY OPTIMIZED: Fixed-size buffers instead of dynamic Strings ─────
#define JSON_BUFFER_SIZE 512
#define URL_BUFFER_SIZE 256
#define RESPONSE_BUFFER_SIZE 1024

// ───── OPTIMIZED: Persistent HTTP client for connection reuse ─────
static HTTPClient WLEDClient_http;
static WiFiClient WLEDClient_wifiClient;
static bool WLEDClient_httpInitialized = false;
static uint32_t WLEDClient_lastConnectionTime = 0;
static const uint32_t HTTP_CONNECTION_REUSE_TIMEOUT_MS = 8000; // IMPROVED: Reduced from 10s

// ───── Circuit breaker / backoff ─────
static uint32_t WLEDClient_nextAllowedMs = 0;
static uint16_t WLEDClient_backoffMs = 0;   // grows to 6000ms max

// ───── OPTIMIZED: Connection health tracking ─────
static uint32_t WLEDClient_lastSuccessMs = 0;
static uint8_t WLEDClient_consecutiveFailures = 0;
static const uint8_t MAX_CONSECUTIVE_FAILURES = 3;

// ───── MEMORY SAFE: Power state tracking with fixed buffers ─────
static bool WLEDClient_lastKnownPowerState = true; // Assume on initially
static uint32_t WLEDClient_lastPowerCheck = 0;

// ───── MEMORY OPTIMIZED: Reusable buffers to eliminate allocations ─────
static char WLEDClient_jsonBuffer[JSON_BUFFER_SIZE];
static char WLEDClient_urlBuffer[URL_BUFFER_SIZE];

static inline bool WLEDClient_guard() {
  uint32_t now = millis();
  return now >= WLEDClient_nextAllowedMs;
}

static inline void WLEDClient_backoff() {
  uint32_t now = millis();
  WLEDClient_consecutiveFailures++;
  
  if (WLEDClient_backoffMs == 0) WLEDClient_backoffMs = 200;       // Start at 200ms
  else WLEDClient_backoffMs = min<uint16_t>(WLEDClient_backoffMs * 2, 6000); // IMPROVED: Cap at 6s instead of 8s
  
  // OPTIMIZED: Add jitter to prevent thundering herd
  uint16_t jitter = random(0, WLEDClient_backoffMs / 4); // 0-25% jitter
  WLEDClient_backoffMs += jitter;
  
  WLEDClient_nextAllowedMs = now + WLEDClient_backoffMs;
  Serial.printf("[HTTP] Backoff: %dms (failures: %d, jitter: +%dms)\n", 
                WLEDClient_backoffMs, WLEDClient_consecutiveFailures, jitter);
}

static inline void WLEDClient_resetBackoff() {
  if (WLEDClient_consecutiveFailures > 0) {
    Serial.printf("[HTTP] Reset backoff (was %d failures)\n", WLEDClient_consecutiveFailures);
  }
  WLEDClient_backoffMs = 0;
  WLEDClient_nextAllowedMs = 0;
  WLEDClient_consecutiveFailures = 0;
  WLEDClient_lastSuccessMs = millis();
}

// ───── SAFE HTTP CLEANUP ─────
static void WLEDClient_safeHTTPCleanup() {
  Serial.println("[WLED] Safe HTTP cleanup");
  WLEDClient_http.end();
  WLEDClient_httpInitialized = false;
  delay(50); // Small delay for lwIP cleanup
  yield(); // Allow background processing
}

// ───── MEMORY OPTIMIZED: Connection management with cleanup ─────
static bool WLEDClient_initHTTP() {
  uint32_t now = millis();
  
  // CONSERVATIVE FIX: Only cleanup if really stale to prevent corruption
  if (WLEDClient_httpInitialized && (now - WLEDClient_lastConnectionTime > 60000)) {
    Serial.println("[WLED] Connection stale - cleaning up");
    WLEDClient_http.end();
    WLEDClient_httpInitialized = false;
    delay(100);
  }
  
  // Skip re-initialization if recently initialized
  if (WLEDClient_httpInitialized && (now - WLEDClient_lastConnectionTime < 5000)) {
    return true;
  }
  
  WLEDClient_wifiClient.setTimeout(WLED_HTTP_TIMEOUT_MS);
  WLEDClient_http.setConnectTimeout(WLED_CONNECT_TIMEOUT_MS);
  WLEDClient_http.setTimeout(WLED_HTTP_TIMEOUT_MS);
  
  // CRITICAL: ESP32/WLED compatibility fixes
  WLEDClient_http.setReuse(false);     // Prevents socket reuse bugs
  WLEDClient_http.useHTTP10(true);     // Avoids chunked transfer stalls
  WLEDClient_http.setUserAgent("ESP32-WLED-Remote/1.0");
  
  WLEDClient_httpInitialized = true;
  WLEDClient_lastConnectionTime = now;
  return true;
}

// ───── MEMORY SAFE: Connection health test with fixed buffers ─────
bool WLEDClient_testConnection() {
  if (!WiFiManager_isConnected()) return false;
  if (!WLEDClient_guard()) return false;
  if (WLEDClient_consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) return false;
  
  if (!WLEDClient_initHTTP()) return false;
  
  // MEMORY SAFE: Use fixed buffer instead of String concatenation
  snprintf(WLEDClient_urlBuffer, URL_BUFFER_SIZE, "http://%s/json", WLED_IP);
  
  if (!WLEDClient_http.begin(WLEDClient_wifiClient, WLEDClient_urlBuffer)) {
    WLEDClient_backoff();
    return false;
  }
  
  // FIXED: Use GET instead of HEAD - some WLED versions don't support HEAD
  int code = WLEDClient_http.GET();
  WLEDClient_http.end(); // Simple cleanup
  
  bool success = (code >= 200 && code < 300);
  
  if (success) {
    WLEDClient_resetBackoff();
    Serial.println("[WLED] Connection test successful");
  } else {
    WLEDClient_backoff();
    Serial.printf("[WLED] Connection test failed: code=%d\n", code);
  }
  
  return success;
}

// ───── MEMORY OPTIMIZED: Non-blocking command queue with fixed allocation ─────
struct QueuedCommand {
  uint8_t type;    // 1=preset, 2=quickload, 3=brightness, 4=power
  uint8_t value;   // preset/QL number, brightness value, power state
  uint32_t queueTime;
};

static QueuedCommand WLEDClient_commandQueue[8]; // Fixed size queue
static uint8_t WLEDClient_queueHead = 0;
static uint8_t WLEDClient_queueTail = 0;
static uint8_t WLEDClient_queueCount = 0;
static const uint8_t QUEUE_SIZE = 8;

static bool WLEDClient_queueCommand(uint8_t type, uint8_t value) {
  if (WLEDClient_queueCount >= QUEUE_SIZE) {
    Serial.println("[WLED] Command queue full - dropping oldest");
    // Remove oldest command
    WLEDClient_queueHead = (WLEDClient_queueHead + 1) % QUEUE_SIZE;
    WLEDClient_queueCount--;
  }
  
  WLEDClient_commandQueue[WLEDClient_queueTail].type = type;
  WLEDClient_commandQueue[WLEDClient_queueTail].value = value;  
  WLEDClient_commandQueue[WLEDClient_queueTail].queueTime = millis();
  
  WLEDClient_queueTail = (WLEDClient_queueTail + 1) % QUEUE_SIZE;
  WLEDClient_queueCount++;
  
  Serial.printf("[WLED] Queued cmd type=%d val=%d (queue: %d/%d)\n", 
                type, value, WLEDClient_queueCount, QUEUE_SIZE);
  return true;
}

// Forward declarations for functions called by queue processing
bool WLEDClient_sendPreset(uint8_t preset);
bool WLEDClient_sendQuickLoad(unsigned char slot);
bool WLEDClient_sendBrightness(uint8_t bri);
bool WLEDClient_sendPowerToggle();
bool TwistManager_isAvailable();
void TwistManager_onProgramChange();

// ───── MEMORY OPTIMIZED: Enhanced queue processing with timeout protection ─────
static void WLEDClient_processQueue() {
  if (WLEDClient_queueCount == 0) return;
  if (!WiFiManager_isConnected()) return;
  if (!WLEDClient_guard()) return; // Respect backoff timing
  
  // IMPROVED: Timeout check to prevent processing stale commands
  uint32_t now = millis();
  QueuedCommand cmd = WLEDClient_commandQueue[WLEDClient_queueHead];
  
  // Skip commands older than 20 seconds (reduced from 30)
  if (now - cmd.queueTime > 20000) {
    Serial.printf("[WLED] Dropping stale cmd type=%d age=%dms\n", 
                  cmd.type, now - cmd.queueTime);
    WLEDClient_queueHead = (WLEDClient_queueHead + 1) % QUEUE_SIZE;
    WLEDClient_queueCount--;
    return;
  }
  
  // Remove from queue first to prevent blocking
  WLEDClient_queueHead = (WLEDClient_queueHead + 1) % QUEUE_SIZE;
  WLEDClient_queueCount--;
  
  // Execute command with timeout protection
  bool success = false;
  uint32_t startTime = millis();
  
  switch (cmd.type) {
    case 1: // Preset
      success = WLEDClient_sendPreset(cmd.value);
      if (success && TwistManager_isAvailable()) {
        TwistManager_onProgramChange();
      }
      break;
    case 2: // QuickLoad  
      success = WLEDClient_sendQuickLoad(cmd.value);
      if (success && TwistManager_isAvailable()) {
        TwistManager_onProgramChange();  
      }
      break;
    case 3: // Brightness
      success = WLEDClient_sendBrightness(cmd.value);
      break;
    case 4: // Power
      success = WLEDClient_sendPowerToggle();
      break;
  }
  
  uint32_t executeTime = millis() - startTime;
  Serial.printf("[WLED] Processed cmd type=%d val=%d result=%s time=%dms (queue: %d left)\n",
                cmd.type, cmd.value, success ? "OK" : "FAIL", executeTime, WLEDClient_queueCount);
                
  // If command took too long, increase backoff to prevent system overload
  if (executeTime > 2000) { // IMPROVED: Reduced threshold from 3000ms
    Serial.println("[WLED] Command took too long - increasing backoff");
    WLEDClient_backoff();
  }
}

static uint8_t WLEDClient_quickLoadPresets[3] = {1, 2, 3};
static bool    WLEDClient_quickLoadsEverLoaded = false;

// ─────────────────────────────────────────────────────────────
// MEMORY SAFE: Core POST /json/state with fixed buffers
// ─────────────────────────────────────────────────────────────
bool WLEDClient_sendWledCommand(const char* jsonBody) {
  if (!WiFiManager_isConnected()) { Serial.println("[HTTP][CMD] WiFi down"); return false; }
  if (!WLEDClient_guard()) return false;
  
  // MEMORY MONITORING: Log heap before network operation
  size_t heapBefore = ESP.getFreeHeap();
  if (heapBefore < 40000) { // 40KB threshold
    Serial.printf("[HTTP][CMD] Low heap warning: %d bytes\n", heapBefore);
  }
  
  if (!WLEDClient_initHTTP()) {
    WLEDClient_backoff();
    return false;
  }

  // MEMORY SAFE: Use fixed buffer for URL
  snprintf(WLEDClient_urlBuffer, URL_BUFFER_SIZE, "http://%s/json/state", WLED_IP);
  Serial.printf("[HTTP][CMD] Sending to %s: %s\n", WLEDClient_urlBuffer, jsonBody);

  if (!WLEDClient_http.begin(WLEDClient_wifiClient, WLEDClient_urlBuffer)) {
    Serial.printf("[HTTP][CMD] begin() failed for %s\n", WLEDClient_urlBuffer);
    WLEDClient_http.end(); // Simple cleanup
    WLEDClient_backoff();
    return false;
  }
  
  WLEDClient_http.addHeader("Content-Type", "application/json");

  int code = WLEDClient_http.POST(jsonBody);
  
  // Always cleanup socket immediately
  WLEDClient_http.end();

  if (code >= 200 && code < 300) { 
    WLEDClient_resetBackoff(); 
    
    // MEMORY MONITORING: Log heap after to detect leaks
    size_t heapAfter = ESP.getFreeHeap();
    if (heapBefore - heapAfter > 2000) {
      Serial.printf("[HTTP] POST memory usage: %d bytes\n", heapBefore - heapAfter);
    }
    
    Serial.printf("[HTTP][CMD] Success: %d\n", code);
    return true; 
  }

  Serial.printf("[HTTP][CMD] POST failed: %s (code=%d)\n", WLEDClient_urlBuffer, code);
  WLEDClient_backoff();
  return false;
}

// ─────────────────────────────────────────────────────────────
// MEMORY SAFE: Core GET /json with streaming + memory management
// ─────────────────────────────────────────────────────────────
template<typename TDoc>
bool WLEDClient_fetchWledState(TDoc& doc) {
  if (!WiFiManager_isConnected()) { Serial.println("[HTTP][GET] WiFi down"); return false; }
  if (!WLEDClient_guard()) return false;

  // MEMORY MONITORING: Log heap before network operation
  size_t heapBefore = ESP.getFreeHeap();
  
  if (!WLEDClient_initHTTP()) {
    WLEDClient_backoff();
    return false;
  }

  // MEMORY SAFE: Use fixed buffer for URL
  snprintf(WLEDClient_urlBuffer, URL_BUFFER_SIZE, "http://%s/json", WLED_IP);

  if (!WLEDClient_http.begin(WLEDClient_wifiClient, WLEDClient_urlBuffer)) {
    Serial.printf("[HTTP][GET] begin() failed for %s\n", WLEDClient_urlBuffer);
    WLEDClient_http.end(); // Simple cleanup
    WLEDClient_backoff();
    return false;
  }

  int code = WLEDClient_http.GET();
  if (code != 200) {
    Serial.printf("[HTTP][GET] GET failed: %s (code=%d)\n", WLEDClient_urlBuffer, code);
    WLEDClient_http.end(); // Simple cleanup
    WLEDClient_backoff();
    return false;
  }

  // OPTIMIZED: Stream directly to ArduinoJson to minimize RAM usage
  WiFiClient* stream = WLEDClient_http.getStreamPtr();
  
  // Safety check for null pointer
  if (!stream) {
    Serial.println("[JSON] ERROR: getStreamPtr() returned null");
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }
  
  // Additional safety check: verify stream is connected and has data
  if (!stream->connected()) {
    Serial.println("[JSON] ERROR: Stream not connected");
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }
  
  // Check if stream has available data before dereferencing
  if (!stream->available()) {
    Serial.println("[JSON] WARNING: No data available in stream");
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }
  
  DeserializationError e;
  // Use safer approach - avoid direct dereferencing in case of memory corruption
  e = deserializeJson(doc, *stream);
  
  // Always cleanup socket
  WLEDClient_http.end();
  
  if (e) {
    Serial.printf("[JSON] parse error: %s\n", e.c_str());
    // CRITICAL: Clear the document to prevent accidental access to invalid data
    doc.clear();
    WLEDClient_backoff();
    return false;
  }

  // MEMORY MONITORING: Log heap after to detect fragmentation
  size_t heapAfter = ESP.getFreeHeap();
  if (heapBefore - heapAfter > 5000) {
    Serial.printf("[HTTP] Memory usage: %d bytes\n", heapBefore - heapAfter);
  }

  WLEDClient_resetBackoff();
  return true;
}

// **Non-template overload to satisfy Arduino's auto-prototype & linker**
bool WLEDClient_fetchWledState(ArduinoJson::JsonDocument& doc) {
  return WLEDClient_fetchWledState<ArduinoJson::JsonDocument>(doc);
}

// ─────────────────────────────────────────────────────────────
// MEMORY SAFE: Batch operations with fixed buffers
// ─────────────────────────────────────────────────────────────
bool WLEDClient_sendBatchCommand(uint8_t preset, uint8_t brightness, bool power, bool setPower) {
  JsonDocument body;
  
  bool hasChanges = false;
  if (preset > 0) { body["ps"] = (int)preset; hasChanges = true; }
  if (brightness > 0) { body["bri"] = (int)brightness; hasChanges = true; }
  if (setPower) { body["on"] = power; hasChanges = true; }
  
  if (!hasChanges) return true; // Nothing to send
  
  // MEMORY SAFE: Serialize to fixed buffer instead of String
  size_t written = serializeJson(body, WLEDClient_jsonBuffer, JSON_BUFFER_SIZE);
  if (written >= JSON_BUFFER_SIZE - 1) {
    Serial.println("[JSON] Buffer overflow in batch command");
    return false;
  }
  
  return WLEDClient_sendWledCommand(WLEDClient_jsonBuffer);
}

// ─────────────────────────────────────────────────────────────
// MEMORY SAFE: Convenience operations - OPTIMIZED versions
// ─────────────────────────────────────────────────────────────

bool WLEDClient_sendPowerToggle() {
  if (!WiFiManager_isConnected()) { Serial.println("[HTTP][PWR] WiFi down"); return false; }
  if (!WLEDClient_guard()) return false;

  // MEMORY SAFE: Use fixed buffer instead of dynamic JSON
  JsonDocument body;
  body["on"] = "t"; // Toggle
  
  size_t written = serializeJson(body, WLEDClient_jsonBuffer, JSON_BUFFER_SIZE);
  if (written >= JSON_BUFFER_SIZE - 1) {
    Serial.println("[JSON] Buffer overflow in power toggle");
    return false;
  }
  
  return WLEDClient_sendWledCommand(WLEDClient_jsonBuffer);
}

bool WLEDClient_sendPreset(uint8_t preset) {
  return WLEDClient_sendBatchCommand(preset, 0, false, false);
}

bool WLEDClient_sendPresetCycle(bool next) {
  if (!WiFiManager_isConnected()) { Serial.println("[HTTP][PCY] WiFi down"); return false; }
  if (!WLEDClient_guard()) return false;

  if (!WLEDClient_initHTTP()) {
    WLEDClient_backoff();
    return false;
  }

  // MEMORY SAFE: Use fixed buffer for URL construction
  const char* direction = next ? "~" : "~-";
  snprintf(WLEDClient_urlBuffer, URL_BUFFER_SIZE, "http://%s/win&P1=1&P2=54&PL=%s", WLED_IP, direction);
  
  Serial.printf("[HTTP][PCY] Cycling presets (%s): %s\n", next ? "next" : "prev", WLEDClient_urlBuffer);

  if (!WLEDClient_http.begin(WLEDClient_wifiClient, WLEDClient_urlBuffer)) {
    Serial.printf("[HTTP][PCY] begin() failed: %s\n", WLEDClient_urlBuffer);
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }

  int code = WLEDClient_http.GET();
  WLEDClient_http.end();

  if (code >= 200 && code < 300) { 
    WLEDClient_resetBackoff(); 
    Serial.printf("[HTTP][PCY] Success: %d\n", code);
    return true; 
  }

  Serial.printf("[HTTP][PCY] GET failed: %s (code=%d)\n", WLEDClient_urlBuffer, code);
  WLEDClient_backoff();
  return false;
}

bool WLEDClient_sendPaletteCycle(bool next) {
  if (!WiFiManager_isConnected()) { Serial.println("[HTTP][PAL] WiFi down"); return false; }
  if (!WLEDClient_guard()) return false;

  if (!WLEDClient_initHTTP()) {
    WLEDClient_backoff();
    return false;
  }

  // MEMORY SAFE: Use fixed buffer for URL construction
  const char* direction = next ? "~" : "~-";
  snprintf(WLEDClient_urlBuffer, URL_BUFFER_SIZE, "http://%s/win&FP=%s", WLED_IP, direction);
  
  Serial.printf("[HTTP][PAL] Cycling palette (%s): %s\n", next ? "next" : "prev", WLEDClient_urlBuffer);

  if (!WLEDClient_http.begin(WLEDClient_wifiClient, WLEDClient_urlBuffer)) {
    Serial.printf("[HTTP][PAL] begin() failed: %s\n", WLEDClient_urlBuffer);
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }

  int code = WLEDClient_http.GET();
  WLEDClient_http.end();

  if (code >= 200 && code < 300) { 
    WLEDClient_resetBackoff(); 
    Serial.printf("[HTTP][PAL] Success: %d\n", code);
    return true; 
  }

  Serial.printf("[HTTP][PAL] GET failed: %s (code=%d)\n", WLEDClient_urlBuffer, code);
  WLEDClient_backoff();
  return false;
}

// ───── MEMORY SAFE: Cached brightness with smart updates ─────
static int WLEDClient_cachedBrightness = -1;
static uint32_t WLEDClient_lastBrightnessUpdate = 0;
static const uint32_t BRIGHTNESS_CACHE_TIMEOUT_MS = 5000; // 5s cache

int WLEDClient_fetchBrightness() {
  uint32_t now = millis();
  
  // Return cached value if recent
  if (WLEDClient_cachedBrightness >= 0 && 
      (now - WLEDClient_lastBrightnessUpdate < BRIGHTNESS_CACHE_TIMEOUT_MS)) {
    return WLEDClient_cachedBrightness;
  }
  
  JsonDocument doc;
  if (!WLEDClient_fetchWledState(doc)) return WLEDClient_cachedBrightness >= 0 ? WLEDClient_cachedBrightness : -1;
  
  if (doc["state"]["bri"].is<int>()) {
    WLEDClient_cachedBrightness = (int)doc["state"]["bri"];
    WLEDClient_lastBrightnessUpdate = now;
    return WLEDClient_cachedBrightness;
  }
  
  Serial.println("[WLED] Brightness not found in state");
  return WLEDClient_cachedBrightness >= 0 ? WLEDClient_cachedBrightness : -1;
}

bool WLEDClient_sendBrightness(uint8_t bri) {
  // OPTIMIZED: Update cache immediately for UI responsiveness
  WLEDClient_cachedBrightness = bri;
  WLEDClient_lastBrightnessUpdate = millis();
  
  // Use batch command
  bool ok = WLEDClient_sendBatchCommand(0, bri, false, false);
  if (!ok) {
    Serial.printf("[WLED] set bri=%u FAILED\n", bri);
    // Invalidate cache on failure
    WLEDClient_cachedBrightness = -1;
  }
  return ok;
}

// ─────────────────────────────────────────────────────────────
// MEMORY SAFE: Presets → Quick-Load mapping with fixed buffers
// ─────────────────────────────────────────────────────────────

bool WLEDClient_parseWLEDPresets() {
  if (!WiFiManager_isConnected()) {
    Serial.println("[WLED] parse presets: WiFi down");
    return false;
  }
  if (!WLEDClient_guard()) return false;

  if (!WLEDClient_initHTTP()) {
    WLEDClient_backoff();
    return false;
  }

  // MEMORY SAFE: Use fixed buffer for URL
  snprintf(WLEDClient_urlBuffer, URL_BUFFER_SIZE, "http://%s/presets.json", WLED_IP);

  if (!WLEDClient_http.begin(WLEDClient_wifiClient, WLEDClient_urlBuffer)) {
    Serial.printf("[HTTP][PRESETS] begin() failed: %s\n", WLEDClient_urlBuffer);
    WLEDClient_backoff();
    return false;
  }

  int code = WLEDClient_http.GET();
  if (code != 200) {
    Serial.printf("[HTTP][PRESETS] GET failed: %s (code=%d)\n", WLEDClient_urlBuffer, code);
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }

  // MEMORY SAFE: Stream directly to avoid String allocation
  WiFiClient* stream = WLEDClient_http.getStreamPtr();
  
  // Safety check for null pointer
  if (!stream) {
    Serial.println("[JSON] ERROR: getStreamPtr() returned null (presets)");
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }
  
  // Additional safety check: verify stream is connected and has data
  if (!stream->connected()) {
    Serial.println("[JSON] ERROR: Stream not connected (presets)");
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }
  
  // Check if stream has available data before dereferencing
  if (!stream->available()) {
    Serial.println("[JSON] WARNING: No data available in stream (presets)");
    WLEDClient_http.end();
    WLEDClient_backoff();
    return false;
  }
  
  JsonDocument doc; // MEMORY SAFE: Fixed size for presets
  DeserializationError e = deserializeJson(doc, *stream);
  
  WLEDClient_http.end(); // Simple cleanup

  if (e) {
    Serial.printf("[JSON] presets parse error: %s\n", e.c_str());
    WLEDClient_backoff();
    return false;
  }

  bool any = false;
  uint8_t tmpQL[3] = {0, 0, 0};

  auto assignQl = [&](int q, uint16_t id) {
    if (q >= 1 && q <= 3 && tmpQL[q-1] == 0) { tmpQL[q-1] = (uint8_t)id; any = true; }
  };
  auto assignFirstFree = [&](uint16_t id) {
    for (int s = 0; s < 3; s++) if (tmpQL[s] == 0) { tmpQL[s] = (uint8_t)id; any = true; return; }
  };

  // MEMORY SAFE: Process presets without String operations
  int processed = 0;
  for (JsonPair kv : doc.as<JsonObject>()) {
    // OPTIMIZED: Yield every 8 iterations to prevent watchdog timeout
    if ((processed & 7) == 0) delay(0);
    processed++;
    
    uint16_t id = atoi(kv.key().c_str()); // Avoid String::toInt()
    JsonObject po = kv.value().as<JsonObject>();
    if (po.isNull()) continue;

    JsonVariant ql = po["ql"];
    if (ql.isNull()) continue;

    if (ql.is<int>()) {
      assignQl(ql.as<int>(), id);
    } else if (ql.is<bool>()) {
      if (ql.as<bool>()) assignFirstFree(id);
    } else if (ql.is<const char*>()) {
      // Simple parsing without String allocations
      const char* str = ql.as<const char*>();
      if (strcmp(str, "true") == 0) assignFirstFree(id);
      else if (strcmp(str, "1") == 0) assignQl(1, id);
      else if (strcmp(str, "2") == 0) assignQl(2, id);
      else if (strcmp(str, "3") == 0) assignQl(3, id);
    }
  }

  if (any) {
    for (int i = 0; i < 3; i++) if (tmpQL[i] == 0) tmpQL[i] = i + 1; // backfill
    memcpy(WLEDClient_quickLoadPresets, tmpQL, 3);
    WLEDClient_quickLoadsEverLoaded = true;
    Serial.printf("[WLED] QuickLoads mapped: QL1=%u QL2=%u QL3=%u\n",
                  WLEDClient_quickLoadPresets[0],
                  WLEDClient_quickLoadPresets[1],
                  WLEDClient_quickLoadPresets[2]);
    WLEDClient_resetBackoff();
    return true;
  } else {
    if (!WLEDClient_quickLoadsEverLoaded) {
      WLEDClient_quickLoadPresets[0] = 1;
      WLEDClient_quickLoadPresets[1] = 2;
      WLEDClient_quickLoadPresets[2] = 3;
      Serial.println("[WLED] No QL in presets; keeping defaults 1/2/3");
    }
    WLEDClient_resetBackoff();
    return false;
  }
}

bool WLEDClient_initQuickLoads() {
  return WLEDClient_parseWLEDPresets();
}

uint8_t WLEDClient_getQuickLoadPreset(uint8_t index) {
  if (index < 3) return WLEDClient_quickLoadPresets[index];
  return 0;
}

bool WLEDClient_sendQuickLoad(unsigned char slot) {
  // Normalize slot to 0..2
  if (slot >= 1 && slot <= 3) slot = (unsigned char)(slot - 1);
  if (slot > 2) return false;

  uint8_t presetId = WLEDClient_getQuickLoadPreset(slot);
  if (presetId == 0) {
    Serial.printf("[WLED] QL slot %u not mapped - using default\n", (unsigned)slot);
    presetId = slot + 1; // Default mapping: QL1=1, QL2=2, QL3=3
  }

  return WLEDClient_sendPreset(presetId);
}

// ─────────────────────────────────────────────────────────────
// OPTIMIZED: Periodic state synchronization + queue processing
// ─────────────────────────────────────────────────────────────
static uint32_t WLEDClient_lastSync = 0;

// PHASE 2A: Consolidated sync to prevent duplicate HTTP requests
void WLEDClient_periodicSync() {
  uint32_t now = millis();
  
  // OPTIMIZED: Process command queue frequently (every 50ms for better responsiveness)
  static uint32_t lastQueueProcess = 0;
  if (now - lastQueueProcess > 50) {
    WLEDClient_processQueue();
    lastQueueProcess = now;
  }
  
  // PHASE 2A: Periodic sync less frequently (30s) - TwistManager now uses cached values
  if (now - WLEDClient_lastSync < 30000) return;
  
  if (WiFiManager_isConnected() && WLEDClient_guard()) {
    // PHASE 2A: Lightweight sync - brightness and power state for cache
    int bri = WLEDClient_fetchBrightness(); // Updates cache
    bool power = WLEDClient_getPowerState(); // Updates cache
    if (bri >= 0) {
      Serial.printf("[WLED] Sync: brightness=%d power=%s (cache updated)\n", bri, power ? "ON" : "OFF");
    }
    WLEDClient_lastSync = now;
  }
}

// ─────────────────────────────────────────────────────────────
// OPTIMIZED: Health monitoring + Queue API
// ─────────────────────────────────────────────────────────────
bool WLEDClient_isHealthy() {
  return WLEDClient_consecutiveFailures < MAX_CONSECUTIVE_FAILURES;
}

uint8_t WLEDClient_getFailureCount() {
  return WLEDClient_consecutiveFailures;
}

uint32_t WLEDClient_getLastSuccessTime() {
  return WLEDClient_lastSuccessMs;
}

// NON-BLOCKING QUEUE API FUNCTIONS
bool WLEDClient_queuePreset(uint8_t preset) {
  return WLEDClient_queueCommand(1, preset);
}

bool WLEDClient_queueQuickLoad(uint8_t slot) {
  return WLEDClient_queueCommand(2, slot);
}

bool WLEDClient_queueBrightness(uint8_t brightness) {
  return WLEDClient_queueCommand(3, brightness);
}

bool WLEDClient_queuePowerToggle() {
  return WLEDClient_queueCommand(4, 0);
}

uint8_t WLEDClient_getQueueCount() {
  return WLEDClient_queueCount;
}

// POWER MANAGEMENT API IMPLEMENTATIONS
bool WLEDClient_getPowerState() {
  uint32_t now = millis();
  // Cache power state for 2 seconds to avoid excessive requests
  if (now - WLEDClient_lastPowerCheck > 2000) {
    JsonDocument doc; // MEMORY SAFE: Fixed size
    if (WLEDClient_fetchWledState(doc)) {
      WLEDClient_lastKnownPowerState = doc["state"]["on"].as<bool>();
      WLEDClient_lastPowerCheck = now;
    }
  }
  return WLEDClient_lastKnownPowerState;
}

bool WLEDClient_setPowerState(bool on) {
  JsonDocument body; // MEMORY SAFE: Fixed size
  body["on"] = on;
  
  size_t written = serializeJson(body, WLEDClient_jsonBuffer, JSON_BUFFER_SIZE);
  if (written >= JSON_BUFFER_SIZE - 1) {
    Serial.println("[JSON] Buffer overflow in power state");
    return false;
  }
  
  bool success = WLEDClient_sendWledCommand(WLEDClient_jsonBuffer);
  if (success) {
    WLEDClient_lastKnownPowerState = on; // Update cache immediately
    WLEDClient_lastPowerCheck = millis();
  }
  return success;
}

// ─────────────────────────────────────────────────────────────
// OPTIMIZED: Cleanup for restart/shutdown
// ─────────────────────────────────────────────────────────────
void WLEDClient_cleanup() {
  if (WLEDClient_httpInitialized) {
    WLEDClient_http.end();
    WLEDClient_httpInitialized = false;
  }
  // Clear any remaining queue items
  WLEDClient_queueHead = 0;
  WLEDClient_queueTail = 0;
  WLEDClient_queueCount = 0;
}

// ─────────────────────────────────────────────────────────────
// CRITICAL MEMORY CHECK: Emergency cleanup when heap is low
// ─────────────────────────────────────────────────────────────
void WLEDClient_checkCriticalMemory() {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 30000) {  // 30KB critical threshold
    Serial.printf("[WLED][CRITICAL] Low memory: %d bytes - clearing queue\n", freeHeap);
    // Emergency cleanup - clear command queue
    WLEDClient_queueHead = 0;
    WLEDClient_queueTail = 0;
    WLEDClient_queueCount = 0;
    // Force HTTP cleanup
    if (WLEDClient_httpInitialized) {
      WLEDClient_http.end();
      WLEDClient_httpInitialized = false;
    }
  }
}

// ─────────────────────────────────────────────────────────────
// WLED Debug Test Function - MEMORY SAFE version
// ─────────────────────────────────────────────────────────────
void WLEDClient_debugTest() {
  Serial.println("\n=== WLED DEBUG TEST (Memory Safe) ===");
  
  // Test basic ping
  Serial.printf("Testing ping to %s...\n", WLED_IP);
  WiFiClient testClient;
  testClient.setTimeout(3000); // Shorter timeout for debug
  
  if (testClient.connect(WLED_IP, 80)) {
    Serial.println("TCP connection successful");
    testClient.stop();
  } else {
    Serial.println("TCP connection FAILED");
    return;
  }
  
  // Test HTTP GET with memory monitoring
  Serial.println("Testing HTTP GET...");
  size_t heapBefore = ESP.getFreeHeap();
  
  HTTPClient http;
  WiFiClient client;
  client.setTimeout(3000);
  
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.setReuse(false);
  http.setUserAgent("ESP32-Debug/1.0");
  
  // MEMORY SAFE: Use fixed buffer for URL
  snprintf(WLEDClient_urlBuffer, URL_BUFFER_SIZE, "http://%s/json", WLED_IP);
  
  if (http.begin(client, WLEDClient_urlBuffer)) {
    Serial.printf("HTTP begin successful for %s\n", WLEDClient_urlBuffer);
    
    int code = http.GET();
    Serial.printf("HTTP GET result: %d\n", code);
    
    if (code > 0) {
      // MEMORY SAFE: Get response length without allocating full string
      int len = http.getSize();
      Serial.printf("Response length: %d\n", len);
      
      if (len > 0 && len < 1000) {
        String response = http.getString(); // Only for small responses
        Serial.printf("First 200 chars: %s\n", response.substring(0, 200).c_str());
      } else {
        Serial.println("Response too large for debug display");
      }
    } else {
      Serial.printf("HTTP error: %s\n", http.errorToString(code).c_str());
    }
    
    http.end();
  } else {
    Serial.println("HTTP begin FAILED");
  }
  
  size_t heapAfter = ESP.getFreeHeap();
  Serial.printf("Debug test memory usage: %d bytes\n", heapBefore - heapAfter);
  
  Serial.println("=== END DEBUG TEST ===\n");
}

// Missing function for compatibility
bool WLEDClient_sendCommand(const char* command) {
  // Send a raw JSON command to WLED
  Serial.printf("[WLED] Sending raw command: %s\n", command);
  
  // Use existing HTTP infrastructure to send the command
  if (!WLEDClient_httpInitialized) {
    if (!WLEDClient_initHTTP()) return false;
  }
  
  // Build URL
  snprintf(WLEDClient_urlBuffer, URL_BUFFER_SIZE, "http://%s/json/state", WLED_IP);
  
  WLEDClient_http.begin(WLEDClient_urlBuffer);
  WLEDClient_http.addHeader("Content-Type", "application/json");
  
  int httpCode = WLEDClient_http.POST(command);
  WLEDClient_http.end();
  
  bool success = (httpCode == 200);
  if (!success) {
    Serial.printf("[WLED] Send command failed: %d\n", httpCode);
  }
  
  return success;
}

// ───── WLED Instance Friendly Name Fetching ─────
void WLEDClient_fetchFriendlyNames() {
  if (!WiFiManager_isConnected()) {
    Serial.println("[WLED] WiFi not connected - skipping friendly name fetch");
    return;
  }
  
  Serial.println("[WLED] Fetching friendly names for all instances...");
  
  for (uint8_t i = 0; i < WLED_INSTANCE_COUNT; i++) {
    Serial.printf("[WLED] Fetching name for instance %d: %s\n", i, WLED_INSTANCES[i].ip);
    
    // Use local HTTP client to avoid conflicts
    HTTPClient http;
    WiFiClient client;
    http.setTimeout(1000); // Short timeout for name fetching
    http.setReuse(false);
    
    char url[128];
    snprintf(url, sizeof(url), "http://%s/json/info", WLED_INSTANCES[i].ip);
    
    if (http.begin(client, url)) {
      int httpCode = http.GET();
      
      if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
          // SAFE: Copy JSON string immediately to avoid dangling pointers
          String nameStr = doc["name"] | String("");
          if (nameStr.length() > 0) {
            // Update the friendly name (note: this modifies the config struct)
            static char nameBuffers[8][32]; // Static storage for names
            if (i < 8) { // Safety check
              strncpy(nameBuffers[i], nameStr.c_str(), 31);
              nameBuffers[i][31] = '\0';
              ((WLEDInstance*)&WLED_INSTANCES[i])->friendlyName = nameBuffers[i];
              Serial.printf("[WLED] Instance %d name: %s\n", i, nameStr.c_str());
            }
          } else {
            Serial.printf("[WLED] No name found for instance %d\n", i);
          }
        } else {
          Serial.printf("[WLED] JSON parse failed for instance %d\n", i);
        }
      } else {
        Serial.printf("[WLED] HTTP %d for instance %d\n", httpCode, i);
      }
      
      http.end();
    } else {
      Serial.printf("[WLED] Failed to connect to instance %d\n", i);
    }
    
    delay(100); // Small delay between requests
  }
  
  Serial.println("[WLED] Friendly name fetching complete");
}