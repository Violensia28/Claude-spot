#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// MOT-CycleControl Configuration
// ESP32 + SSR Zero-Cross + OLED + Rotary Encoder
// ============================================================

// -------------------- VERSION --------------------
#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

// -------------------- PIN DEFINITIONS --------------------
// SSR Control (5V direct drive via external board)
#define SSR_PIN           26    // GPIO26 -> 5V rail -> SSR input LED

// Rotary Encoder (KY-040)
#define ENCODER_CLK       32    // CLK (A phase)
#define ENCODER_DT        33    // DT (B phase)
#define ENCODER_SW        25    // Switch (button)

// OLED Display (SSD1306 I2C)
#define OLED_SDA          21    // I2C Data
#define OLED_SCL          22    // I2C Clock
#define OLED_ADDR         0x3C  // I2C address
#define OLED_WIDTH        128   // Pixel width
#define OLED_HEIGHT       64    // Pixel height
#define OLED_RESET        -1    // Reset pin (-1 = shared with ESP32 reset)

// Trigger Button (optional external button, active LOW)
#define TRIGGER_BTN_PIN   27    // GPIO27 with internal pullup
#define TRIGGER_ACTIVE    LOW   // Active state

// -------------------- TIMING CONSTANTS --------------------
#define AC_FREQ_HZ        50    // AC frequency (Indonesia/EU: 50Hz, US: 60Hz)
#define CYCLE_MS          20    // 1000ms / 50Hz = 20ms per cycle
#define MIN_LEVEL         1     // Minimum weld level (1 cycle = 20ms)
#define MAX_LEVEL         99    // Maximum weld level (99 cycles = 1980ms)

// -------------------- WELD MODES --------------------
#define WELD_MODE_SINGLE  0     // Single pulse
#define WELD_MODE_DOUBLE  1     // Double pulse (P1 + GAP + P2)

// -------------------- DEFAULT SETTINGS --------------------
// Conservative defaults for safety
#define DEFAULT_P1        5     // Pulse 1: 100ms (5 cycles)
#define DEFAULT_GAP       3     // Gap: 60ms (3 cycles)
#define DEFAULT_P2        7     // Pulse 2: 140ms (7 cycles)
#define DEFAULT_MODE      WELD_MODE_SINGLE

// -------------------- ENCODER BEHAVIOR --------------------
#define ENCODER_STEP      1     // Increment per detent
#define ENCODER_DEBOUNCE  50    // Button debounce (ms)

// -------------------- UI REFRESH --------------------
#define DISPLAY_UPDATE_MS 100   // OLED refresh interval (ms)
#define ENCODER_READ_MS   10    // Encoder polling interval (ms)

// -------------------- WI-FI AP SETTINGS --------------------
#define AP_SSID           "MOT-CycleControl"
#define AP_PASSWORD       "motspotweld"
#define AP_CHANNEL        6     // Wi-Fi channel
#define AP_MAX_CONN       4     // Max simultaneous connections
#define AP_HIDDEN         false // Broadcast SSID

// -------------------- WEB SERVER --------------------
#define WEB_PORT          80    // HTTP port
#define WEB_UPDATE_PATH   "/update" // OTA endpoint

// -------------------- SAFETY LIMITS --------------------
#define MAX_WELD_TIME_MS  2000  // Absolute maximum weld duration (2 seconds)
#define MIN_COOLDOWN_MS   500   // Minimum time between welds (0.5 seconds)

// -------------------- DEBUG --------------------
#define DEBUG_SERIAL      true  // Enable serial debug output
#define DEBUG_BAUD        115200

// -------------------- DERIVED MACROS --------------------
// Convert level to milliseconds
#define LEVEL_TO_MS(level) ((uint16_t)(level) * CYCLE_MS)

// Constrain level to valid range
#define CONSTRAIN_LEVEL(val) constrain((val), MIN_LEVEL, MAX_LEVEL)

// -------------------- COMPILE-TIME CHECKS --------------------
#if (DEFAULT_P1 < MIN_LEVEL) || (DEFAULT_P1 > MAX_LEVEL)
#error "DEFAULT_P1 out of valid range"
#endif

#if (DEFAULT_GAP < MIN_LEVEL) || (DEFAULT_GAP > MAX_LEVEL)
#error "DEFAULT_GAP out of valid range"
#endif

#if (DEFAULT_P2 < MIN_LEVEL) || (DEFAULT_P2 > MAX_LEVEL)
#error "DEFAULT_P2 out of valid range"
#endif

#endif // CONFIG_H
