// ─────────────────────────────────────────────────────────────
// WiFi Connection Management - OPTIMIZED (auto-reconnect, health monitoring)
// ─────────────────────────────────────────────────────────────

// OPTIMIZED: WiFi state tracking
static bool WiFiManager_isConnected_cached = false;
static uint32_t WiFiManager_lastStatusCheck = 0;
static const uint32_t WIFI_STATUS_CACHE_MS = 100; // Cache status for 100ms

// OPTIMIZED: Connection health monitoring
static uint32_t WiFiManager_lastConnectionTime = 0;
static uint32_t WiFiManager_reconnectAttempts = 0;
static uint32_t WiFiManager_totalDisconnects = 0;
static int WiFiManager_lastRSSI = 0;

// OPTIMIZED: Reconnection backoff
static uint32_t WiFiManager_nextReconnectTime = 0;
static uint16_t WiFiManager_reconnectBackoff = 0;
static const uint16_t MIN_RECONNECT_INTERVAL_MS = 1000;
static const uint16_t MAX_RECONNECT_INTERVAL_MS = 30000;

// OPTIMIZED: Network stack health
static uint32_t WiFiManager_lastStackCheck = 0;
static uint8_t WiFiManager_stackHangCount = 0;

void WiFiManager_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  
  // OPTIMIZED: Set hostname for network identification
  WiFi.setHostname("wled-remote");
  
  // OPTIMIZED: Enable auto-reconnect
  WiFi.setAutoReconnect(true);
  
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  
  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 7000) { 
    delay(200); 
    Serial.print("."); 
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    WiFiManager_lastConnectionTime = millis();
    WiFiManager_reconnectAttempts = 0;
    WiFiManager_reconnectBackoff = 0;
    WiFiManager_isConnected_cached = true;
    WiFiManager_lastRSSI = WiFi.RSSI();
    
    Serial.printf("[WiFi] Connected: %s (RSSI %d dBm)\n", 
                  WiFi.localIP().toString().c_str(), WiFiManager_lastRSSI);
  } else {
    WiFiManager_isConnected_cached = false;
    Serial.println("[WiFi] Initial connection failed - will retry automatically");
  }
}

// OPTIMIZED: Cached connection status with periodic refresh
bool WiFiManager_isConnected() {
  uint32_t now = millis();
  
  // Return cached status if recent
  if (now - WiFiManager_lastStatusCheck < WIFI_STATUS_CACHE_MS) {
    return WiFiManager_isConnected_cached;
  }
  
  WiFiManager_lastStatusCheck = now;
  bool wasConnected = WiFiManager_isConnected_cached;
  WiFiManager_isConnected_cached = (WiFi.status() == WL_CONNECTED);
  
  // Track disconnections
  if (wasConnected && !WiFiManager_isConnected_cached) {
    WiFiManager_totalDisconnects++;
    Serial.printf("[WiFi] Disconnected (total: %d)\n", WiFiManager_totalDisconnects);
  }
  
  // Track reconnections
  if (!wasConnected && WiFiManager_isConnected_cached) {
    WiFiManager_lastConnectionTime = now;
    WiFiManager_reconnectBackoff = 0;
    WiFiManager_lastRSSI = WiFi.RSSI();
    Serial.printf("[WiFi] Reconnected after %d attempts (RSSI %d dBm)\n", 
                  WiFiManager_reconnectAttempts, WiFiManager_lastRSSI);
    WiFiManager_reconnectAttempts = 0;
  }
  
  return WiFiManager_isConnected_cached;
}

int WiFiManager_getRSSI() {
  if (WiFiManager_isConnected()) {
    WiFiManager_lastRSSI = WiFi.RSSI();
  }
  return WiFiManager_lastRSSI;
}

// OPTIMIZED: Non-blocking reconnection with exponential backoff
void WiFiManager_attemptReconnect() {
  uint32_t now = millis();
  
  if (now < WiFiManager_nextReconnectTime) return; // Still in backoff period
  
  if (WiFi.status() == WL_CONNECTED) return; // Already connected
  
  WiFiManager_reconnectAttempts++;
  Serial.printf("[WiFi] Reconnect attempt #%d\n", WiFiManager_reconnectAttempts);
  
  // Non-blocking reconnect
  WiFi.reconnect();
  
  // Exponential backoff
  if (WiFiManager_reconnectBackoff == 0) {
    WiFiManager_reconnectBackoff = MIN_RECONNECT_INTERVAL_MS;
  } else {
    WiFiManager_reconnectBackoff = min((uint16_t)(WiFiManager_reconnectBackoff * 2), 
                                      MAX_RECONNECT_INTERVAL_MS);
  }
  
  WiFiManager_nextReconnectTime = now + WiFiManager_reconnectBackoff;
  
  Serial.printf("[WiFi] Next attempt in %d ms\n", WiFiManager_reconnectBackoff);
}

// OPTIMIZED: Network stack health monitoring
void WiFiManager_checkStackHealth() {
  uint32_t now = millis();
  
  if (now - WiFiManager_lastStackCheck < 60000) return; // Check every minute
  WiFiManager_lastStackCheck = now;
  
  // Detect potential stack hangs (connected but RSSI is 0)
  if (WiFi.status() == WL_CONNECTED && WiFi.RSSI() == 0) {
    WiFiManager_stackHangCount++;
    Serial.printf("[WiFi] Potential stack hang detected (%d/3)\n", WiFiManager_stackHangCount);
    
    if (WiFiManager_stackHangCount >= 3) {
      Serial.println("[WiFi] Stack hang confirmed - forcing reconnect");
      WiFi.disconnect();
      delay(100);
      WiFi.reconnect();
      WiFiManager_stackHangCount = 0;
    }
  } else {
    WiFiManager_stackHangCount = 0; // Reset on healthy connection
  }
}

// OPTIMIZED: Main update function with all monitoring
void WiFiManager_update() {
  uint32_t now = millis();
  static uint32_t lastUpdate = 0;
  
  if (now - lastUpdate < WIFI_CHECK_INTERVAL_MS) return;
  lastUpdate = now;
  
  // Check current status (this updates the cache)
  bool connected = WiFiManager_isConnected();
  
  if (!connected) {
    WiFiManager_attemptReconnect();
  } else {
    // Perform health checks on connected network
    WiFiManager_checkStackHealth();
    
    // Update RSSI for monitoring
    WiFiManager_lastRSSI = WiFi.RSSI();
  }
}

// OPTIMIZED: Connection statistics
void WiFiManager_printStats() {
  Serial.printf("[WiFi] Stats: Connected=%s, RSSI=%ddBm, Disconnects=%d, Reconnects=%d\n",
                WiFiManager_isConnected() ? "Yes" : "No",
                WiFiManager_lastRSSI,
                WiFiManager_totalDisconnects,
                WiFiManager_reconnectAttempts);
  
  if (WiFiManager_isConnected()) {
    Serial.printf("[WiFi] Uptime: %ds, IP: %s\n",
                  (millis() - WiFiManager_lastConnectionTime) / 1000,
                  WiFi.localIP().toString().c_str());
  }
}

// OPTIMIZED: Force reconnection (for external recovery)
void WiFiManager_forceReconnect() {
  Serial.println("[WiFi] Forced reconnection requested");
  WiFi.disconnect();
  WiFiManager_isConnected_cached = false;
  WiFiManager_nextReconnectTime = 0; // Allow immediate reconnect
  WiFiManager_reconnectBackoff = 0;
  WiFiManager_attemptReconnect();
}

// OPTIMIZED: Health check functions
bool WiFiManager_isHealthy() {
  return WiFiManager_isConnected() && WiFiManager_lastRSSI > -80; // Good signal
}

uint32_t WiFiManager_getDisconnectCount() {
  return WiFiManager_totalDisconnects;
}

uint32_t WiFiManager_getUptimeSeconds() {
  return WiFiManager_isConnected() ? (millis() - WiFiManager_lastConnectionTime) / 1000 : 0;
}

int8_t WiFiManager_getSignalStrength() {
  if (!WiFiManager_isConnected()) return -100;
  
  int rssi = WiFiManager_lastRSSI;
  if (rssi >= -50) return 4;      // Excellent
  else if (rssi >= -60) return 3; // Good  
  else if (rssi >= -70) return 2; // Fair
  else if (rssi >= -80) return 1; // Weak
  else return 0;                  // Very weak
}

// Missing function for compatibility
void WiFiManager_enterPowerSave() {
  // Implement power saving mode for WiFi
  WiFi.setSleep(WIFI_PS_MAX_MODEM);
  Serial.println("[WIFI] Entered power save mode");
}