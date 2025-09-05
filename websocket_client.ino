// ─────────────────────────────────────────────────────────────
// WebSocket WLED Client - PHASE 3: Low-Latency Real-Time Communication
// Provides bidirectional WebSocket communication with WLED for minimal latency
// ─────────────────────────────────────────────────────────────

#if ENABLE_WEBSOCKET_WLED

#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ───── WebSocket State Management ─────
static WebSocketsClient WebSocketClient_ws;
static bool WebSocketClient_connected = false;
static bool WebSocketClient_initialized = false;
static uint32_t WebSocketClient_lastConnectionAttempt = 0;
static uint32_t WebSocketClient_lastPing = 0;
static uint32_t WebSocketClient_connectionTime = 0;

// ───── Message Queue for WebSocket Commands ─────
struct WSMessage {
  char payload[256];
  uint8_t type;
  uint32_t timestamp;
};

static WSMessage WebSocketClient_messageQueue[8];
static uint8_t WebSocketClient_queueHead = 0;
static uint8_t WebSocketClient_queueTail = 0;
static uint8_t WebSocketClient_queueCount = 0;

// ───── Statistics ─────
static uint32_t WebSocketClient_messagesSent = 0;
static uint32_t WebSocketClient_messagesReceived = 0;
static uint32_t WebSocketClient_reconnections = 0;
static uint32_t WebSocketClient_failedSends = 0;
static uint32_t WebSocketClient_avgLatency = 0;

// ───── Forward Declarations ─────
bool WiFiManager_isConnected();
void FreqManager_notifyNetworkActivity();
bool TwistManager_isAvailable();
void TwistManager_setBrightness(int brightness);

// ───── WebSocket Event Handler ─────
void WebSocketClient_onEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Disconnected from %s\n", WLED_IP);
      WebSocketClient_connected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.printf("[WS] Connected to %s\n", (char*)payload);
      WebSocketClient_connected = true;
      WebSocketClient_connectionTime = millis();
      WebSocketClient_lastPing = millis();
      
      // Send initial connection message
      WebSocketClient_ws.sendTXT("{\"v\":true}"); // Request current state
      break;
      
    case WStype_TEXT:
      {
        Serial.printf("[WS] Received: %s\n", (char*)payload);
        WebSocketClient_messagesReceived++;
        FreqManager_notifyNetworkActivity();
        
        // Parse received JSON for state updates
        if (!payload) {
          Serial.println("[WS] ERROR: Null payload received");
          break;
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (char*)payload);
        
        if (!error) {
          // CRITICAL FIX: Additional validation before processing
          if (!doc.isNull() && doc.size() > 0) {
            WebSocketClient_handleStateUpdate(doc);
          } else {
            Serial.println("[WS] Document is null or empty after successful parse");
          }
        } else {
          Serial.printf("[WS] JSON parse error: %s\n", error.c_str());
          // CRITICAL FIX: Ensure document is cleared on error
          doc.clear();
          doc = JsonDocument(); // Reset to completely empty state
        }
      }
      break;
      
    case WStype_BIN:
      Serial.printf("[WS] Binary data received (%d bytes)\n", length);
      break;
      
    case WStype_ERROR:
      Serial.printf("[WS] Error: %s\n", (char*)payload);
      break;
      
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      Serial.println("[WS] Fragment received");
      break;
      
    case WStype_PING:
      Serial.println("[WS] Ping received");
      break;
      
    case WStype_PONG:
      {
        Serial.println("[WS] Pong received");
        // Calculate latency
        uint32_t latency = millis() - WebSocketClient_lastPing;
        WebSocketClient_avgLatency = (WebSocketClient_avgLatency * 7 + latency) / 8; // Running average
      }
      break;
  }
}

// ───── State Update Handler ─────
void WebSocketClient_handleStateUpdate(JsonDocument& doc) {
  // CRITICAL FIX: Comprehensive validation before accessing document
  if (doc.isNull() || doc.size() == 0) {
    Serial.println("[WS] Invalid document passed to state handler");
    return;
  }
  
  // Handle WLED state updates received via WebSocket
  if (doc.containsKey("state") && doc["state"].is<JsonObject>()) {
    JsonObject state = doc["state"];
    
    if (state.containsKey("on") && state["on"].is<bool>()) {
      bool powerState = state["on"];
      Serial.printf("[WS] Power state update: %s\n", powerState ? "ON" : "OFF");
    }
    
    if (state.containsKey("bri") && state["bri"].is<int>()) {
      int brightness = state["bri"];
      Serial.printf("[WS] Brightness update: %d\n", brightness);
      
      // Notify twist manager of brightness change
      if (TwistManager_isAvailable()) {
        TwistManager_setBrightness(brightness);
      }
    }
    
    if (state.containsKey("seg")) {
      Serial.println("[WS] Segment state update received");
    }
  }
  
  if (doc.containsKey("info")) {
    Serial.println("[WS] Info update received");
  }
  
  if (doc.containsKey("effects")) {
    Serial.println("[WS] Effects list received");
  }
  
  if (doc.containsKey("palettes")) {
    Serial.println("[WS] Palettes list received");
  }
}

// ───── Message Queue Management ─────
bool WebSocketClient_queueMessage(const char* message, uint8_t type = 1) {
  if (WebSocketClient_queueCount >= 8) {
    Serial.println("[WS] Message queue full");
    return false;
  }
  
  strncpy(WebSocketClient_messageQueue[WebSocketClient_queueTail].payload, message, 255);
  WebSocketClient_messageQueue[WebSocketClient_queueTail].payload[255] = '\0';
  WebSocketClient_messageQueue[WebSocketClient_queueTail].type = type;
  WebSocketClient_messageQueue[WebSocketClient_queueTail].timestamp = millis();
  
  WebSocketClient_queueTail = (WebSocketClient_queueTail + 1) % 8;
  WebSocketClient_queueCount++;
  
  return true;
}

void WebSocketClient_processQueue() {
  if (!WebSocketClient_connected || WebSocketClient_queueCount == 0) return;
  
  uint32_t now = millis();
  WSMessage msg = WebSocketClient_messageQueue[WebSocketClient_queueHead];
  
  // Skip messages older than 10 seconds
  if (now - msg.timestamp > 10000) {
    Serial.println("[WS] Dropping stale message");
    WebSocketClient_queueHead = (WebSocketClient_queueHead + 1) % 8;
    WebSocketClient_queueCount--;
    return;
  }
  
  // Send the message
  bool success = WebSocketClient_ws.sendTXT(msg.payload);
  
  if (success) {
    WebSocketClient_messagesSent++;
    Serial.printf("[WS] Sent: %s\n", msg.payload);
    FreqManager_notifyNetworkActivity();
  } else {
    WebSocketClient_failedSends++;
    Serial.printf("[WS] Failed to send: %s\n", msg.payload);
  }
  
  // Remove from queue
  WebSocketClient_queueHead = (WebSocketClient_queueHead + 1) % 8;
  WebSocketClient_queueCount--;
}

// ───── Connection Management ─────
bool WebSocketClient_connect() {
  if (!WiFiManager_isConnected()) {
    return false;
  }
  
  if (WebSocketClient_connected) {
    return true;
  }
  
  uint32_t now = millis();
  if (now - WebSocketClient_lastConnectionAttempt < WEBSOCKET_RECONNECT_INTERVAL) {
    return false; // Too soon to retry
  }
  
  WebSocketClient_lastConnectionAttempt = now;
  Serial.printf("[WS] Connecting to ws://%s:81/\n", WLED_IP);
  
  // WLED WebSocket is typically on port 81
  WebSocketClient_ws.begin(WLED_IP, 81, "/");
  WebSocketClient_ws.onEvent(WebSocketClient_onEvent);
  WebSocketClient_ws.setReconnectInterval(WEBSOCKET_RECONNECT_INTERVAL);
  
  WebSocketClient_reconnections++;
  return true;
}

void WebSocketClient_disconnect() {
  if (WebSocketClient_connected) {
    WebSocketClient_ws.disconnect();
    WebSocketClient_connected = false;
    Serial.println("[WS] Disconnected");
  }
}

// ───── High-Level WLED Commands via WebSocket ─────
bool WebSocketClient_sendPreset(uint8_t preset) {
  if (!WebSocketClient_connected) return false;
  
  char message[64];
  snprintf(message, sizeof(message), "{\"ps\":%d}", preset);
  return WebSocketClient_queueMessage(message, 1);
}

bool WebSocketClient_sendBrightness(uint8_t brightness) {
  if (!WebSocketClient_connected) return false;
  
  char message[64];
  snprintf(message, sizeof(message), "{\"bri\":%d}", brightness);
  return WebSocketClient_queueMessage(message, 1);
}

bool WebSocketClient_sendPowerToggle() {
  if (!WebSocketClient_connected) return false;
  
  return WebSocketClient_queueMessage("{\"on\":\"t\"}", 1);
}

bool WebSocketClient_sendPowerState(bool on) {
  if (!WebSocketClient_connected) return false;
  
  char message[32];
  snprintf(message, sizeof(message), "{\"on\":%s}", on ? "true" : "false");
  return WebSocketClient_queueMessage(message, 1);
}

bool WebSocketClient_sendEffect(uint8_t effectId) {
  if (!WebSocketClient_connected) return false;
  
  char message[64];
  snprintf(message, sizeof(message), "{\"seg\":[{\"fx\":%d}]}", effectId);
  return WebSocketClient_queueMessage(message, 1);
}

bool WebSocketClient_sendPalette(uint8_t paletteId) {
  if (!WebSocketClient_connected) return false;
  
  char message[64];
  snprintf(message, sizeof(message), "{\"seg\":[{\"pal\":%d}]}", paletteId);
  return WebSocketClient_queueMessage(message, 1);
}

bool WebSocketClient_requestState() {
  if (!WebSocketClient_connected) return false;
  
  return WebSocketClient_queueMessage("{\"v\":true}", 1);
}

// ───── Main WebSocket Client Functions ─────
void WebSocketClient_init() {
  if (!ENABLE_WEBSOCKET_WLED) {
    Serial.println("[WS] WebSocket WLED communication disabled");
    return;
  }
  
  Serial.println("[WS] Initializing WebSocket WLED client...");
  
  // Initialize queue
  WebSocketClient_queueHead = 0;
  WebSocketClient_queueTail = 0;
  WebSocketClient_queueCount = 0;
  
  // Reset statistics
  WebSocketClient_messagesSent = 0;
  WebSocketClient_messagesReceived = 0;
  WebSocketClient_reconnections = 0;
  WebSocketClient_failedSends = 0;
  WebSocketClient_avgLatency = 0;
  
  WebSocketClient_initialized = true;
  Serial.println("[WS] WebSocket client initialized");
}

void WebSocketClient_update() {
  if (!ENABLE_WEBSOCKET_WLED || !WebSocketClient_initialized) return;
  
  // Handle WebSocket events
  WebSocketClient_ws.loop();
  
  // Attempt connection if not connected
  if (!WebSocketClient_connected) {
    WebSocketClient_connect();
  } else {
    // Process outgoing message queue
    WebSocketClient_processQueue();
    
    // Send periodic ping to keep connection alive
    uint32_t now = millis();
    if (now - WebSocketClient_lastPing > WEBSOCKET_PING_INTERVAL) {
      WebSocketClient_ws.sendPing();
      WebSocketClient_lastPing = now;
    }
  }
}

// ───── Status and Statistics ─────
bool WebSocketClient_isConnected() {
  return ENABLE_WEBSOCKET_WLED && WebSocketClient_connected;
}

bool WebSocketClient_isInitialized() {
  return ENABLE_WEBSOCKET_WLED && WebSocketClient_initialized;
}

uint8_t WebSocketClient_getQueueDepth() {
  return WebSocketClient_queueCount;
}

uint32_t WebSocketClient_getUptime() {
  return WebSocketClient_connected ? (millis() - WebSocketClient_connectionTime) : 0;
}

void WebSocketClient_printStats() {
  Serial.printf("[WS] Stats: Connected=%s, Sent=%d, Received=%d, Failed=%d, Queue=%d/8\n",
                WebSocketClient_connected ? "Yes" : "No",
                WebSocketClient_messagesSent, WebSocketClient_messagesReceived,
                WebSocketClient_failedSends, WebSocketClient_queueCount);
  
  if (WebSocketClient_connected) {
    Serial.printf("[WS] Uptime=%ds, Reconnections=%d, AvgLatency=%dms\n",
                  WebSocketClient_getUptime() / 1000, WebSocketClient_reconnections,
                  WebSocketClient_avgLatency);
  }
}

uint32_t WebSocketClient_getMessagesSent() {
  return WebSocketClient_messagesSent;
}

uint32_t WebSocketClient_getMessagesReceived() {
  return WebSocketClient_messagesReceived;
}

uint32_t WebSocketClient_getAverageLatency() {
  return WebSocketClient_avgLatency;
}

// ───── Cleanup ─────
void WebSocketClient_cleanup() {
  if (WebSocketClient_initialized) {
    WebSocketClient_disconnect();
    WebSocketClient_initialized = false;
    Serial.println("[WS] WebSocket client cleaned up");
  }
}

bool WebSocketClient_queueMessage(const char* message) {
  // Queue a raw message for sending
  if (WebSocketClient_queueCount >= 8) {
    Serial.println("[WS] Queue full, dropping message");
    return false;
  }
  
  strncpy(WebSocketClient_messageQueue[WebSocketClient_queueCount].payload, message, 255);
  WebSocketClient_messageQueue[WebSocketClient_queueCount].payload[255] = '\0';
  WebSocketClient_messageQueue[WebSocketClient_queueCount].type = 0; // Default type
  WebSocketClient_messageQueue[WebSocketClient_queueCount].timestamp = millis();
  WebSocketClient_queueCount++;
  return true;
}

#else // !ENABLE_WEBSOCKET_WLED

// ───── Stub Functions When WebSocket is Disabled ─────
void WebSocketClient_init() {}
void WebSocketClient_update() {}
bool WebSocketClient_sendPreset(uint8_t preset) { return false; }
bool WebSocketClient_sendBrightness(uint8_t brightness) { return false; }
bool WebSocketClient_sendPowerToggle() { return false; }
bool WebSocketClient_sendPowerState(bool on) { return false; }
bool WebSocketClient_sendEffect(uint8_t effectId) { return false; }
bool WebSocketClient_sendPalette(uint8_t paletteId) { return false; }
bool WebSocketClient_requestState() { return false; }
bool WebSocketClient_isConnected() { return false; }
bool WebSocketClient_isInitialized() { return false; }
uint8_t WebSocketClient_getQueueDepth() { return 0; }
uint32_t WebSocketClient_getUptime() { return 0; }
void WebSocketClient_printStats() { Serial.println("[WS] WebSocket disabled"); }
uint32_t WebSocketClient_getMessagesSent() { return 0; }
uint32_t WebSocketClient_getMessagesReceived() { return 0; }
uint32_t WebSocketClient_getAverageLatency() { return 0; }
void WebSocketClient_cleanup() {}
bool WebSocketClient_queueMessage(const char* message) { return false; }

#endif // ENABLE_WEBSOCKET_WLED