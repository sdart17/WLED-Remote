// ─────────────────────────────────────────────────────────────
// Configuration and Constants - OPTIMIZED with Brightness Page Control
// ─────────────────────────────────────────────────────────────

#ifndef CONFIG_INO_INCLUDED
#define CONFIG_INO_INCLUDED

// ───────── CRITICAL: UI Configuration MUST BE FIRST ─────────
// Set to true to enable the brightness slider page (page 2)
// Set to false to hide it and use only hardware encoder for brightness
// NOTE: This is also defined in main file - keep values synchronized!
#ifndef ENABLE_BRIGHTNESS_PAGE
#define ENABLE_BRIGHTNESS_PAGE false
#endif

// Set to true to enable WLED instance selection page
// Set to false to use only the default first instance
#ifndef ENABLE_WLED_SELECTION_PAGE
#define ENABLE_WLED_SELECTION_PAGE true
#endif

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include <CSE_CST328.h>

// ───────── WiFi Configuration ─────────
static const char* WIFI_SSID = "Darts";
static const char* WIFI_PWD  = "abcdef1234";

// ───────── WLED Configuration - Multi-Instance Support ─────────
typedef struct {
  const char* ip;
  const char* friendlyName;  // Will be populated from WLED /json/info
} WLEDInstance;

// WLED instance list - add your WLED devices here
static WLEDInstance WLED_INSTANCES[] = {
  {"192.168.6.220", "Scott Bedroom"},
  {"192.168.4.42", "Hexagons"},
  {"192.168.4.76", "Seattle Skyline"},
};

static const uint8_t WLED_INSTANCE_COUNT = sizeof(WLED_INSTANCES) / sizeof(WLEDInstance);
static uint8_t CURRENT_WLED_INSTANCE = 0;  // Default to first instance

static const uint16_t HTTP_TIMEOUT_MS = 500; // AGGRESSIVE: Ultra-fast timeout

// Legacy compatibility - points to current selected instance
#define WLED_IP (WLED_INSTANCES[CURRENT_WLED_INSTANCE].ip)

// ───────── PHASE 3: WebSocket WLED Communication ─────────
#define ENABLE_WEBSOCKET_WLED false       // DISABLED: Battery drain and connection instability
#define WEBSOCKET_RECONNECT_INTERVAL 5000 // Reconnect every 5s if disconnected
#define WEBSOCKET_PING_INTERVAL 30000     // Ping every 30s to keep connection alive
#define WEBSOCKET_TIMEOUT_MS 2000         // WebSocket operation timeout

// ───────── Performance Configuration ─────────
// Split update intervals for optimal responsiveness
#define TOUCH_UPDATE_INTERVAL_MS 1       // Ultra-fast touch response
#define TWIST_UPDATE_INTERVAL_MS 16      // Hardware encoder (60 FPS)
#define DISPLAY_UPDATE_INTERVAL_MS 33    // Display refresh (30 FPS)
#define WIFI_CHECK_INTERVAL_MS 10000     // WiFi health check
#define WLED_SYNC_INTERVAL_MS 30000      // Periodic WLED state sync

// ───────── PHASE 2B: FreeRTOS Dual-Core Configuration ─────────
#define ENABLE_DUAL_CORE_SEPARATION true  // Enable network task on core 0
#define UI_CORE 1                         // UI/Touch/Display on core 1 (default)
#define NETWORK_CORE 0                    // WiFi/HTTP/WLED on core 0
#define NETWORK_TASK_STACK_SIZE 8192      // Stack size for network task
#define NETWORK_TASK_PRIORITY 1           // Lower priority than UI
#define NETWORK_QUEUE_SIZE 16             // Command queue size

// ───────── PHASE 3: Dynamic Frequency Scaling ─────────
#define ENABLE_DYNAMIC_FREQ_SCALING true  // Enable CPU frequency scaling
#define CPU_FREQ_HIGH_MHZ 240             // High performance mode (240MHz)
#define CPU_FREQ_NORMAL_MHZ 160            // Normal operation mode (160MHz) 
#define CPU_FREQ_LOW_MHZ 80                // Power saving mode (80MHz)
#define FREQ_SCALE_INACTIVITY_THRESHOLD_MS 10000  // Downscale after 10s inactivity
#define FREQ_SCALE_ACTIVITY_THRESHOLD_MS 2000     // Upscale within 2s of activity

// ───────── Memory Management ─────────
#define HEAP_WARNING_THRESHOLD 50000     // 50KB minimum free heap
#define LARGE_BMP_THRESHOLD 76800        // 240x320 pixels max
#define JSON_STACK_SIZE 2048             // Fixed JSON document size

// ───────── PHASE 3: Advanced Memory Management ─────────
#define ENABLE_MEMORY_OPTIMIZATION true  // Enable advanced memory optimization
#define MEMORY_POOL_SIZE 32768           // 32KB memory pool for frequent allocations
#define PSRAM_CACHE_SIZE 131072          // 128KB PSRAM cache for large assets
#define MEMORY_DEFRAG_THRESHOLD 0.7f     // Trigger defrag when 70% fragmented
#define MEMORY_GC_INTERVAL_MS 30000      // Garbage collection every 30s

// ───────── PHASE 4: Advanced UI and Intelligence ─────────
#define ENABLE_SMOOTH_ANIMATIONS true         // Enable smooth UI transitions and animations
#define ENABLE_INTELLIGENT_POWER true         // Enable intelligent power management
#define ENABLE_PREDICTIVE_SCALING true        // Enable predictive performance scaling
#define ENABLE_DIFFERENTIAL_RENDERING false   // DISABLED: Causing UI corruption during boot
#define ENABLE_SMART_CACHING true             // Enable contextual asset preloading
#define ENABLE_SYSTEM_TELEMETRY true          // Enable system telemetry and optimization

// ───────── Display Pin Configuration ─────────
#define PIN_LCD_MOSI 45
#define PIN_LCD_SCLK 40
#define PIN_LCD_CS   42
#define PIN_LCD_DC   41
#define PIN_LCD_RST  39
#define PIN_LCD_BL    5

// ───────── Touch Pin Configuration ─────────
#define PIN_TP_SDA   1
#define PIN_TP_SCL   3
#define PIN_TP_RST   2
#define PIN_TP_INT   4

// ───────── SD Card Pin Configuration ─────────
#define PIN_SD_SCK  14
#define PIN_SD_MISO 16
#define PIN_SD_MOSI 17
#define PIN_SD_CS   21
static const uint32_t SD_SPI_HZ = 40000000; // OPTIMIZED: Increased from 20MHz

// ───────── Power Pin Configuration ─────────
#define PIN_BAT_HOLD 7

// ───────── Display Constants ─────────
static const uint16_t SCREEN_WIDTH = 240;
static const uint16_t SCREEN_HEIGHT = 320;
// Dynamic screen timeout based on power source
static const uint32_t SCREEN_TIMEOUT_BATTERY_MS = 30UL * 1000UL;  // 30 seconds on battery
static const uint32_t SCREEN_TIMEOUT_PLUGGED_MS = 5UL * 60UL * 1000UL;  // 5 minutes when plugged in

// Function to get current screen timeout based on power source
inline uint32_t getScreenTimeout() {
  return PowerManager_isExternalPower() ? SCREEN_TIMEOUT_PLUGGED_MS : SCREEN_TIMEOUT_BATTERY_MS;
}

// Legacy compatibility - use function instead
#define SCREEN_TIMEOUT_MS getScreenTimeout()

// ───────── UI Constants - UPDATED ─────────
// Dynamic page count based on brightness page setting - USE DIFFERENT NAME
// NOTE: TOTAL_UI_PAGES is also defined in main file for early access
#if ENABLE_BRIGHTNESS_PAGE
static const int TOTAL_UI_PAGES_CONFIG = 4;  // Pages 0, 1, 2, 3 (includes brightness page)
#else
static const int TOTAL_UI_PAGES_CONFIG = 3;  // Pages 0, 1, 3 (skips brightness page)
#endif

// ───────── Touch Constants ─────────
static const int16_t SWIPE_THRESHOLD = 25;
static const int16_t VERTICAL_TOLERANCE = 80;
static const uint32_t SWIPE_TIMEOUT_MS = 500;

// ───────── PHASE 3: Advanced Touch Gesture Recognition ─────────
#define ENABLE_ADVANCED_GESTURES true       // Enable advanced gesture recognition
#define ENABLE_MULTI_TOUCH false            // Multi-touch detection (experimental)
#define ENABLE_TOUCH_PRESSURE true          // Touch pressure sensitivity
#define GESTURE_LONG_PRESS_TIME_MS 800      // Long press threshold
#define GESTURE_DOUBLE_TAP_TIME_MS 300      // Double tap window
#define GESTURE_PINCH_THRESHOLD 15          // Pinch gesture minimum distance
#define TOUCH_PRESSURE_THRESHOLD 3          // Pressure sensitivity levels

// ───────── Brightness Slider Constants ─────────
static const int SLIDER_ACTIVE_HALF_WIDTH = 10;
static const int SLIDER_Y_PAD = 6;

// ───────── Icon Cache Constants ─────────
static const int MAX_ICON_CACHE = 24; // PHASE 2A: Increased from 16 to accommodate all preloaded icons

// ───────── Battery Constants ─────────
static const uint32_t BATTERY_UPDATE_INTERVAL_MS = 1500;

// ───────── Touch Watchdog Constants ─────────
static const uint32_t TOUCH_IDLE_TIMEOUT_MS = 8000;
static const uint32_t TOUCH_REINIT_INTERVAL_MS = 30000;

// ───────── Twist Manager Configuration ─────────
static const uint32_t TWIST_PALETTE_FADE_INTERVAL_MS = 5000;  // Time between palette colors
static const uint8_t TOUCH_SENSITIVITY_THRESHOLD = 10;        // Touch sensitivity (1-255)

// ───────── Power Management Constants - NEW ─────────
static const uint32_t SLEEP_MODE_DELAY_MS = 1000;           // Delay before entering sleep mode
static const uint32_t WAKE_DEBOUNCE_MS = 200;               // Debounce time for wake events

// ───────── Error Codes ─────────
enum SystemError {
  SYS_OK = 0,
  SYS_WIFI_DOWN = 1,
  SYS_WLED_TIMEOUT = 2,
  SYS_WLED_SERVER_ERROR = 3,
  SYS_TOUCH_FAIL = 4,
  SYS_SD_ERROR = 5,
  SYS_MEMORY_LOW = 6,
  SYS_JSON_PARSE_ERROR = 7
};

enum WLEDError {
  WLED_OK = 0,
  WLED_NETWORK_ERROR = 1,    // Retry with backoff
  WLED_TIMEOUT_ERROR = 2,    // Retry immediately once
  WLED_SERVER_ERROR = 3,     // Don't retry, log issue
  WLED_PARSE_ERROR = 4       // Local issue, check payload
};

// ───────── Power Mode States - NEW ─────────
enum PowerMode {
  POWER_NORMAL = 0,     // Normal operation - screen on, encoder LED on
  POWER_SAVING = 1,     // Power saving - screen off, encoder LED off, WLED off
  POWER_WAKE = 2        // Temporary state during wake-up
};

// Power Management Types for Twist Manager
enum TwistPowerMode {
  TWIST_POWER_NORMAL = 0,     // Normal operation - screen on, encoder LED on
  TWIST_POWER_SAVING = 1,     // Power saving - screen off, encoder LED off, WLED off
  TWIST_POWER_TRANSITION = 2  // Temporary state during mode changes
};

#endif // CONFIG_INO_INCLUDED