// ─────────────────────────────────────────────────────────────
// Power and Battery Management - FIXED EXTERNAL POWER DETECTION
// ─────────────────────────────────────────────────────────────

// Power manager state variables
static bool PowerManager_batHoldAvailable = false;
static bool PowerManager_batteryEnabled = false;
static int PowerManager_batteryPin = 8;
static uint32_t PowerManager_lastBatteryUpdate = 0;

// PHASE 2B: Ultra-fast power detection for <1s response
static bool PowerManager_lastExternalState = false;
static uint32_t PowerManager_lastExternalCheck = 0;
static const uint32_t EXTERNAL_POWER_CHECK_INTERVAL_MS = 200; // PHASE 2B: Check every 200ms for <1s response

void PowerManager_powerHoldInit() {
  pinMode(PIN_BAT_HOLD, OUTPUT);
  digitalWrite(PIN_BAT_HOLD, HIGH);
  PowerManager_batHoldAvailable = true;
  Serial.printf("[PWR] BAT_Control asserted on GPIO%d (HIGH)\n", PIN_BAT_HOLD);
}

void PowerManager_powerHoldReleaseAndOff() {
  if (PowerManager_batHoldAvailable) {
    Serial.println("[PWR] Releasing BAT_Control LOW (shutdown)...");
    digitalWrite(PIN_BAT_HOLD, LOW);
    delay(1500);
  }
  Serial.println("[PWR] If still powered, entering deep sleep.");
  esp_deep_sleep_start();
}

void PowerManager_initBattery() {
  Serial.println("[BAT] Initializing battery monitor...");
  int candidates[] = {8, 33, 34, 36, 37};
  for (int i = 0; i < (int)(sizeof(candidates)/sizeof(candidates[0])); i++) {
    analogSetPinAttenuation(candidates[i], ADC_11db);
    delay(3);
    int mv = analogReadMilliVolts(candidates[i]);
    Serial.printf("[BAT] Pin %d: %dmV\n", candidates[i], mv);
    if (mv > 500 && mv < 2200) {
      PowerManager_batteryPin = candidates[i];
      PowerManager_batteryEnabled = true;
      Serial.printf("[BAT] cell ≈ %dmV\n", mv*3);
      return;
    }
  }
  Serial.println("[BAT] No BAT_ADC found; battery UI disabled");
}

void PowerManager_init() {
  PowerManager_powerHoldInit();
  PowerManager_initBattery();
  
  // Initialize external power state
  PowerManager_lastExternalState = PowerManager_checkExternalPower();
  PowerManager_lastExternalCheck = millis();
  
  Serial.printf("[PWR] Initial external power state: %s\n", 
                PowerManager_lastExternalState ? "EXTERNAL" : "BATTERY");
}

void PowerManager_update() {
  uint32_t now = millis();
  
  // Update external power detection periodically
  if (now - PowerManager_lastExternalCheck > EXTERNAL_POWER_CHECK_INTERVAL_MS) {
    bool newState = PowerManager_checkExternalPower();
    if (newState != PowerManager_lastExternalState) {
      PowerManager_lastExternalState = newState;
      Serial.printf("[PWR] Power source changed: %s\n", 
                    newState ? "EXTERNAL" : "BATTERY");
    }
    PowerManager_lastExternalCheck = now;
  }
}

void PowerManager_shutdown() {
  PowerManager_powerHoldReleaseAndOff();
}

void PowerManager_restart() {
  ESP.restart();
}

int PowerManager_getBatteryVoltage() {
  if (!PowerManager_batteryEnabled) return 3800;
  analogSetPinAttenuation(PowerManager_batteryPin, ADC_11db);
  delay(2);
  int mv = analogReadMilliVolts(PowerManager_batteryPin);
  return mv * 3;
}

int PowerManager_getBatteryPercent() {
  int mv = PowerManager_getBatteryVoltage();
  if (mv <= 3300) return 1;
  if (mv >= 4200) return 100;
  return (mv - 3300) * 100 / (4200 - 3300);
}

// IMPROVED: Better external power detection with multiple methods
bool PowerManager_checkExternalPower() {
  // STABILIZED: More reliable detection for ESP32-S3-Touch board
  
  // Method 1: Check battery voltage - if unusually high, likely charging
  int battVoltage = PowerManager_getBatteryVoltage();
  
  // Method 2: More nuanced USB detection
  bool usbActive = Serial && Serial.availableForWrite() > 0;
  
  // STABILIZED: Use more conservative thresholds to reduce oscillation
  // Normal LiPo range is 3.3V-4.2V, but we need stability over precision
  bool voltageIndicatesCharging = (battVoltage > 4180); // Raised threshold
  
  // Method 3: Check if battery percentage is suspiciously high with debouncing
  int battPercent = PowerManager_getBatteryPercent();
  
  // STABILIZED: Use hysteresis for percentage to prevent oscillation
  static bool wasHighPercent = false;
  bool percentHigh = (battPercent >= 98); // Only very high percentages
  bool percentLow = (battPercent <= 95);  // Allow some hysteresis
  
  if (percentHigh) wasHighPercent = true;
  else if (percentLow) wasHighPercent = false;
  // Otherwise keep previous state
  
  bool percentIndicatesCharging = wasHighPercent && (battVoltage > 4120);
  
  // PHASE 2B: Ultra-fast state detection for <1s response
  static uint8_t externalCount = 0;
  static uint8_t batteryCount = 0;
  
  bool rawExternalPower = voltageIndicatesCharging || percentIndicatesCharging || usbActive;
  
  // Count consecutive readings in same direction
  if (rawExternalPower) {
    externalCount = min(3, externalCount + 1); // PHASE 2B: Reduced from 6 to 3 max
    batteryCount = max(0, batteryCount - 1);
  } else {
    batteryCount = min(2, batteryCount + 1);   // PHASE 2B: Reduced from 4 to 2 max
    externalCount = max(0, externalCount - 1);
  }
  
  // PHASE 2B: Ultra-fast response - require only 2 readings for external, 1 for battery
  bool stableExternalPower;
  if (externalCount >= 2) {        // PHASE 2B: Only 2 readings needed (400ms total)
    stableExternalPower = true;
  } else if (batteryCount >= 1) {  // PHASE 2B: Only 1 reading needed (200ms total)
    stableExternalPower = false;
  } else {
    // Keep previous state if not enough evidence
    stableExternalPower = PowerManager_lastExternalState;
  }
  
  // PHASE 2B: Ultra-fast debug logging for immediate feedback
  static uint32_t lastDebugLog = 0;
  if (millis() - lastDebugLog > 1500) {  // PHASE 2B: Every 1.5s for faster feedback
    Serial.printf("[PWR] V=%dmV (%d%%), USB=%s, VChg=%s, %%Chg=%s, Ext=%d/3, Bat=%d/2 -> %s\n",
                  battVoltage, battPercent,
                  usbActive ? "Y" : "N",
                  voltageIndicatesCharging ? "Y" : "N", 
                  percentIndicatesCharging ? "Y" : "N",
                  externalCount, batteryCount,  // PHASE 2B: Updated max values (3/2)
                  stableExternalPower ? "EXTERNAL" : "BATTERY");
    lastDebugLog = millis();
  }
  
  return stableExternalPower;
}

bool PowerManager_isExternalPower() {
  return PowerManager_lastExternalState;
}

bool PowerManager_isBatteryEnabled() {
  return PowerManager_batteryEnabled;
}

// DIAGNOSTIC: Force external power state for testing
void PowerManager_forceExternalPowerState(bool external) {
  PowerManager_lastExternalState = external;
  Serial.printf("[PWR] FORCED external power state: %s\n", 
                external ? "EXTERNAL" : "BATTERY");
}

// Missing function - alias for compatibility
uint32_t PowerManager_getBatteryLevel() {
  return (uint32_t)PowerManager_getBatteryPercent();
}