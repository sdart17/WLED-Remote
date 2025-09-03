# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview
Arduino C++ project for ESP32-S3-Touch-2.8 hardware implementing a WLED remote control with 240x320 touchscreen, rotary encoder with RGB LED, and SD card storage.

## Development Environment
- **IDE**: Arduino IDE v2.3.3 (required)
- **Target**: ESP32-S3 microcontroller 
- **Board Package**: ESP32 Arduino Core v3.0.5+
- **Libraries**: TFT_eSPI, ArduinoJson, WiFi, SD

## Development Commands
Since this is an Arduino project, use Arduino IDE buttons:
- **Compile**: Arduino IDE Verify button
- **Upload**: Arduino IDE Upload button  
- **Serial Monitor**: Arduino IDE Serial Monitor (115200 baud)
- **Debug**: Serial.printf() with performance timing logs

## Architecture Overview

### Manager-Based Design
The codebase uses a modular manager pattern with 8 core components:

1. **PowerManager** (`power_manager.ino`) - Battery monitoring, power states, sleep management
2. **DisplayManager** (`display_manager.ino`) - ST7789 TFT rendering with double buffering optimization
3. **WiFiManager** (`wifi_manager.ino`) - Network connectivity with auto-reconnection
4. **WLEDClient** (`wled_client.ino`) - HTTP REST API communication with circuit breaker pattern
5. **TouchManager** (`touch_manager.ino`) - Capacitive touch with gesture recognition and debouncing
6. **TwistManager** (`twist_manager.ino`) - Rotary encoder input with integrated RGB LED control
7. **UIManager** (`ui_manager.ino`) - Multi-page interface with swipe navigation and state management
8. **SDManager** (`sd_manager.ino`) - Asset loading with intelligent caching and memory pressure handling

### Performance Architecture
**Differentiated Update Frequencies** for optimal responsiveness:
- Touch input polling: 1ms (ultra-responsive)
- Display refresh: 33ms (30 FPS)
- Network health checks: 10s intervals
- WLED synchronization: 30s periodic updates

## Arduino-Specific Constraints

### Code Style Requirements
- **C-style code only** - no C++ classes or objects
- All implementation files must be `.ino` for Arduino's automatic inclusion
- No separate header files - use forward declarations in main file
- Manager pattern with consistent `[component]_manager.ino` naming
- Zero regressions policy - code must compile cleanly

### Memory Management
- **Fixed-size buffers** instead of dynamic allocation
- Aggressive connection cleanup to prevent memory leaks  
- Smart asset caching with pressure-aware eviction
- Circuit breaker pattern for network resilience

## Key Configuration Files
- `/config.ino` - Hardware pin definitions, network settings, feature flags
- `/WLED-Remote-0831.ino` - Main entry point with setup() and loop()
- `/Hardware-software details.txt` - Complete hardware integration documentation
- `/Issues.txt` - Known optimization targets and performance bottlenecks

## Development Guidelines
- Follow manager pattern for new subsystems
- Maintain differentiated update frequencies for performance
- Use Serial.printf() with timing metrics for debugging
- Test touch responsiveness with 1ms polling requirement
- Validate memory usage under sustained operation
- Ensure WiFi resilience with connection recovery

## Feature Flags
- `ENABLE_BRIGHTNESS_PAGE` - Controls brightness page visibility in UI navigation

## Network Integration
WLED communication uses HTTP REST API with endpoints:
- `/json/state` - Device state and control
- `/json/info` - Device information and capabilities
- Circuit breaker pattern prevents cascade failures during network issues