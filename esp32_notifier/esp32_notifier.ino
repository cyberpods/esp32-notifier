/*
 * ESP32-Notifier
 * Version: 3.2
 * Multi-channel notification system with web configuration
 *
 * New in v3.2:
 * - BMP180 temperature and pressure sensor support
 * - Real-time environmental monitoring (temperature, pressure, altitude)
 * - Configurable I2C pins for sensor connection
 * - Automatic sensor data inclusion in notifications
 * - Live sensor readings in web interface
 *
 * Features from v3.1:
 * - Cellular/4G support with SIM7670G modem
 * - Intelligent connection modes (WiFi Only, Cellular Only, WiFi+Cellular Backup)
 * - SMS notifications via cellular modem
 * - HTTP over cellular for Pushbullet and Telegram
 * - Per-service connection mode configuration
 * - Cellular status monitoring (operator, signal strength)
 *
 * Features from v3.0:
 * - ESP32-S3-SIM7670G-4G board support
 * - OV2640 camera integration with photo capture
 * - GPS/GNSS positioning support
 * - Photo attachments in notifications
 * - SD card storage for photos
 * - Board type selection
 *
 * Features from v2.0:
 * - Multi-input support (up to 4 switches)
 * - WiFi auto-reconnection
 * - Watchdog timer
 * - Web authentication
 * - Non-blocking operations
 * - Notification retry logic
 * - Rate limiting
 * - Test buttons for services
 * - ArduinoJson for safety
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// Fix for ESP32-S3-SIM7670G USB Serial issue - redirect to hardware UART
// This board's USB CDC doesn't work properly, but hardware UART does
#define Serial Serial0

// Optional advanced libraries - will gracefully disable if not available
// Comment out any line below if you don't have that library installed
#define HAS_EMAIL_LIB
#define HAS_CAMERA_LIB
#define HAS_GPS_LIB
#define HAS_BMP180_LIB

#ifdef HAS_EMAIL_LIB
  #include <ESP_Mail_Client.h>
#endif

#ifdef HAS_CAMERA_LIB
  #include "esp_camera.h"
  #include <SD_MMC.h>
  #include <SD.h>  // Alternative SPI-based SD library
  #include <SPI.h>
  #include <FS.h>
#endif

#ifdef HAS_GPS_LIB
  #include <TinyGPSPlus.h>
#endif

#ifdef HAS_BMP180_LIB
  #include <Wire.h>
  #include <Adafruit_BMP085.h>  // Works with both BMP085 and BMP180
#endif

#define VERSION "3.2-BMP180"
#define MAX_INPUTS 4
#define WDT_TIMEOUT 30

// Board type definitions
#define BOARD_GENERIC 0
#define BOARD_SIM7670G 1
#define BOARD_FREENOVE_S3 2

// Connection mode definitions
#define CONN_WIFI_ONLY 0
#define CONN_CELL_ONLY 1
#define CONN_WIFI_CELL_BACKUP 2

// SIM7670G modem pins
// NOTE: GPIO 43/44 are Serial0 (used for debug output), so we use GPIO 17/18 instead
// Modem pins based on Waveshare example
#define MODEM_TX_PIN 17
#define MODEM_RX_PIN 18
#define MODEM_PWR_EN_PIN 33   // Power enable pin (CRITICAL - must be HIGH)
#define MODEM_PWRKEY_PIN 41   // Power key pin
#define MODEM_RESET_PIN 42    // Reset pin

// Camera pin definitions for ESP32-S3-SIM7670G-4G (Waveshare)
#define SIM7670G_CAM_PWDN    -1
#define SIM7670G_CAM_RESET   -1
#define SIM7670G_CAM_XCLK    34
#define SIM7670G_CAM_SIOD    15
#define SIM7670G_CAM_SIOC    16
#define SIM7670G_CAM_Y9      14
#define SIM7670G_CAM_Y8      13
#define SIM7670G_CAM_Y7      12
#define SIM7670G_CAM_Y6      11
#define SIM7670G_CAM_Y5      10
#define SIM7670G_CAM_Y4      9
#define SIM7670G_CAM_Y3      8
#define SIM7670G_CAM_Y2      7
#define SIM7670G_CAM_VSYNC   36
#define SIM7670G_CAM_HREF    35
#define SIM7670G_CAM_PCLK    37

// Camera pin definitions for Freenove ESP32-S3
// Based on datasheet: GPIO11,9,8,10,12,18,17,16 for data
#define FREENOVE_CAM_PWDN    -1
#define FREENOVE_CAM_RESET   -1
#define FREENOVE_CAM_XCLK    15
#define FREENOVE_CAM_SIOD    4   // I2C SDA
#define FREENOVE_CAM_SIOC    5   // I2C SCL
#define FREENOVE_CAM_Y9      16
#define FREENOVE_CAM_Y8      17
#define FREENOVE_CAM_Y7      18
#define FREENOVE_CAM_Y6      12
#define FREENOVE_CAM_Y5      10
#define FREENOVE_CAM_Y4      8
#define FREENOVE_CAM_Y3      9
#define FREENOVE_CAM_Y2      11
#define FREENOVE_CAM_VSYNC   6
#define FREENOVE_CAM_HREF    7
#define FREENOVE_CAM_PCLK    13

// Legacy pin definitions (for backward compatibility with existing code)
// These will be set dynamically based on board type
#define CAM_PIN_PWDN    SIM7670G_CAM_PWDN
#define CAM_PIN_RESET   SIM7670G_CAM_RESET
#define CAM_PIN_XCLK    SIM7670G_CAM_XCLK
#define CAM_PIN_SIOD    SIM7670G_CAM_SIOD
#define CAM_PIN_SIOC    SIM7670G_CAM_SIOC
#define CAM_PIN_Y9      SIM7670G_CAM_Y9
#define CAM_PIN_Y8      SIM7670G_CAM_Y8
#define CAM_PIN_Y7      SIM7670G_CAM_Y7
#define CAM_PIN_Y6      SIM7670G_CAM_Y6
#define CAM_PIN_Y5      SIM7670G_CAM_Y5
#define CAM_PIN_Y4      SIM7670G_CAM_Y4
#define CAM_PIN_Y3      SIM7670G_CAM_Y3
#define CAM_PIN_Y2      SIM7670G_CAM_Y2
#define CAM_PIN_VSYNC   SIM7670G_CAM_VSYNC
#define CAM_PIN_HREF    SIM7670G_CAM_HREF
#define CAM_PIN_PCLK    SIM7670G_CAM_PCLK

// SD card pins for ESP32-S3-SIM7670G-4G (Waveshare)
#define SIM7670G_SD_CLK      5
#define SIM7670G_SD_CMD      4
#define SIM7670G_SD_DATA     6
#define SIM7670G_SD_CD       46

// SD card pins for Freenove ESP32-S3 (uses default SDMMC pins)
#define FREENOVE_SD_CLK      39  // GPIO39
#define FREENOVE_SD_CMD      38  // GPIO38
#define FREENOVE_SD_DATA     40  // GPIO40
#define FREENOVE_SD_CD       -1  // No card detect

// Backward compatibility
#define SD_MMC_CLK      SIM7670G_SD_CLK
#define SD_MMC_CMD      SIM7670G_SD_CMD
#define SD_MMC_DATA     SIM7670G_SD_DATA
#define SD_CD_PIN       SIM7670G_SD_CD

// GPS serial - using GNSS output from SIM7670G via AT commands
// GPS data will be retrieved via AT+CGNSINF from the modem

// Web server
WebServer server(80);
Preferences preferences;

#ifdef HAS_EMAIL_LIB
  SMTPSession* smtp = nullptr;  // Will be initialized when needed
#endif

// Board configuration
int board_type = BOARD_GENERIC;
bool camera_enabled = false;
bool gps_enabled = false;
bool sd_card_available = false;
bool camera_initialized = false;

#ifdef HAS_GPS_LIB
  TinyGPSPlus* gps = nullptr;  // Will be initialized only if needed
  HardwareSerial* GPS_Serial = nullptr;  // Will be initialized in setup()
#endif

// BMP180 sensor configuration
#ifdef HAS_BMP180_LIB
  Adafruit_BMP085* bmp = nullptr;  // Will be initialized only if needed
#endif
bool bmp180_enabled = false;
bool bmp180_initialized = false;
bool bmp180_needs_reinit = false;  // Flag to defer reinit until after HTTP response
int bmp180_sda_pin = 21;  // Default I2C SDA pin (Header 2, Pin 22)
int bmp180_scl_pin = 33;  // Default I2C SCL pin (Header 2, Pin 16 - SPI0_4, may be available)

// Deferred operations flags (to prevent blocking during HTTP requests)
bool needs_save_prefs = false;  // Defer savePreferences() call
bool needs_time_update = false;  // Defer configTime() call
long pending_gmt_offset = 0;
int pending_daylight_offset = 0;
float last_temperature = 0.0;
float last_pressure = 0.0;
float last_altitude = 0.0;
bool include_bmp180_in_notifications = true;  // Include sensor data in notifications by default

// Cellular modem configuration
HardwareSerial* ModemSerial = nullptr;  // Will be initialized in setup()
bool cellular_enabled = false;
bool cellular_connected = false;
String cellular_operator = "";
int cellular_signal_strength = 0;
String apn = ""; // APN for cellular data
String modem_model = "";
String modem_imei = "";
String modem_iccid = "";
String modem_network_type = "";
bool modem_initialized = false;

// Connection modes per service
int pushbullet_conn_mode = CONN_WIFI_ONLY;
int email_conn_mode = CONN_WIFI_ONLY;
int telegram_conn_mode = CONN_WIFI_ONLY;
int sms_conn_mode = CONN_CELL_ONLY;

// SMS configuration
String sms_phone_number = "";
bool sms_enabled = false;

// WiFi state management
enum WiFiState { WIFI_IDLE, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED };
WiFiState wifiState = WIFI_IDLE;
unsigned long wifiConnectStart = 0;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_TIMEOUT = 20000;
const unsigned long WIFI_CHECK_INTERVAL = 30000;
bool wifiReconnecting = false;
bool apMode = false;
const char* AP_SSID = "ESP32-Notifier-Setup";
const char* AP_PASSWORD = "setup123";

// Time sync
bool timeIsSynced = false;
unsigned long lastTimeSyncAttempt = 0;
const unsigned long TIME_SYNC_RETRY = 3600000;

// Configuration
String wifi_ssid = "";
String wifi_password = "";
String web_username = "admin";
String web_password = "admin123";

// Pushbullet
String pushbullet_token = "";
bool pushbullet_enabled = false;

// Email
String smtp_host = "smtp.gmail.com";
int smtp_port = 465;
String smtp_email = "";
String smtp_password = "";
String recipient_email = "";
bool email_enabled = false;

// Telegram
String telegram_token = "";
String telegram_chat_id = "";
bool telegram_enabled = false;

// Messages
String notification_title = "Device Status Alert";

// Time
long gmt_offset = 0;  // UTC by default
int daylight_offset = 0;
const char* ntpServer = "pool.ntp.org";

// Input configuration structure
struct InputConfig {
  int pin;
  bool enabled;
  bool momentary_mode;
  bool capture_photo;  // NEW: Take photo when triggered
  bool include_gps;    // NEW: Include GPS in notification
  String name;
  String message_on;
  String message_off;
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  unsigned long lastNotificationTime;
};

// Default pins to avoid conflicts:
// - Modem uses: 17, 18, 41, 42
// - Camera uses: 4-16, 34-37
// - SD card uses: 4, 5, 6, 46
// - Strapping pins (avoid): 0, 1, 2, 3, 45, 46
// Safe GPIO pins for SIM7670G: 21, 38, 39, 40, 47, 48
InputConfig inputs[MAX_INPUTS] = {
  {21, true, false, false, false, "Input 1", "Input 1 ON at {timestamp}", "Input 1 OFF at {timestamp}", LOW, LOW, 0, 0},
  {38, false, false, false, false, "Input 2", "Input 2 ON at {timestamp}", "Input 2 OFF at {timestamp}", LOW, LOW, 0, 0},
  {39, false, false, false, false, "Input 3", "Input 3 ON at {timestamp}", "Input 3 OFF at {timestamp}", LOW, LOW, 0, 0},
  {40, false, false, false, false, "Input 4", "Input 4 ON at {timestamp}", "Input 4 OFF at {timestamp}", LOW, LOW, 0, 0}
};

const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long MIN_NOTIFICATION_INTERVAL = 5000;

// Notification retry queue
struct NotificationRetry {
  String title;
  String body;
  unsigned long retryTime;
  int retryCount;
  String service;
};

std::vector<NotificationRetry> retryQueue;
const int MAX_RETRIES = 3;
const unsigned long RETRY_DELAY = 60000;

// Logging system
struct LogEntry {
  String timestamp;
  String level;     // INFO, WARNING, ERROR, SUCCESS
  String message;
};

const int MAX_LOG_ENTRIES = 100;
LogEntry logBuffer[MAX_LOG_ENTRIES];
int logIndex = 0;
int logCount = 0;

// ========================================
// FORWARD DECLARATIONS
// ========================================

// Core functions
String getFormattedTime();
void addLog(String level, String message);

// Notification functions
void sendNotifications(String title, String body, String photoFile = "");
bool sendPushbulletNotification(String title, String body, String photoFile = "");
bool sendEmailNotification(String subject, String body, String photoFile = "");
bool sendTelegramNotification(String message, String photoFile = "");

// Cellular functions
bool sendSMS(String phoneNumber, String message);
String sendHTTPRequest(String url, String method, String payload = "", String contentType = "application/json");

// ========================================
// IMPLEMENTATION
// ========================================

void addLog(String level, String message) {
  // During early boot, just print to serial without complex operations
  static bool earlyBoot = true;

  if (earlyBoot) {
    Serial.print(F("[EARLY-"));
    Serial.print(level);
    Serial.print(F("] "));
    Serial.println(message);
    Serial.flush();
    // After first few calls, switch to normal mode
    static int callCount = 0;
    if (++callCount > 3) earlyBoot = false;
    return;
  }

  LogEntry entry;
  entry.timestamp = getFormattedTime();
  entry.level = level;
  entry.message = message;

  logBuffer[logIndex] = entry;
  logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
  if (logCount < MAX_LOG_ENTRIES) logCount++;

  // Also print to Serial
  Serial.print(F("["));
  Serial.print(entry.timestamp);
  Serial.print(F("] ["));
  Serial.print(level);
  Serial.print(F("] "));
  Serial.println(message);
  Serial.flush();
}

// Preference keys
namespace PrefKeys {
  const char* WIFI_SSID = "wifi_ssid";
  const char* WIFI_PASS = "wifi_pass";
  const char* WEB_USER = "web_user";
  const char* WEB_PASS = "web_pass";
  const char* PB_TOKEN = "pb_token";
  const char* PB_ENABLED = "pb_enabled";
  const char* SMTP_HOST = "smtp_host";
  const char* SMTP_PORT = "smtp_port";
  const char* SMTP_EMAIL = "smtp_email";
  const char* SMTP_PASS = "smtp_pass";
  const char* RCPT_EMAIL = "rcpt_email";
  const char* EMAIL_ENABLED = "email_enabled";
  const char* TG_TOKEN = "tg_token";
  const char* TG_CHAT = "tg_chat";
  const char* TG_ENABLED = "tg_enabled";
  const char* NOTIF_TITLE = "notif_title";
  const char* GMT_OFFSET = "gmt_offset";
  const char* DAY_OFFSET = "day_offset";
  // NEW: v3.0 preferences
  const char* BOARD_TYPE = "board_type";
  const char* CAMERA_EN = "camera_en";
  const char* GPS_EN = "gps_en";
  // NEW: v3.1 cellular preferences
  const char* CELL_EN = "cell_en";
  const char* APN = "apn";
  const char* PB_CONN_MODE = "pb_conn";
  const char* EMAIL_CONN_MODE = "email_conn";
  const char* TG_CONN_MODE = "tg_conn";
  const char* SMS_CONN_MODE = "sms_conn";
  const char* SMS_EN = "sms_en";
  const char* SMS_PHONE = "sms_phone";
  // BMP180 sensor preferences
  const char* BMP180_EN = "bmp180_en";
  const char* BMP180_SDA = "bmp180_sda";
  const char* BMP180_SCL = "bmp180_scl";
  const char* BMP180_IN_NOTIF = "bmp180_notif";
}

// ========================================
// CAMERA FUNCTIONS
// ========================================

#ifdef HAS_CAMERA_LIB
bool initCamera() {
  if (board_type == BOARD_GENERIC) {
    addLog("INFO", "Camera not available - board type is generic");
    return false;
  }

  Serial.println(F("Initializing OV2640 camera..."));
  Serial.printf("Free heap before camera init: %d bytes\n", ESP.getFreeHeap());
  if (psramFound()) {
    Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  }
  Serial.flush();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  // Set camera pins based on board type
  if (board_type == BOARD_SIM7670G) {
    config.pin_d0 = SIM7670G_CAM_Y2;
    config.pin_d1 = SIM7670G_CAM_Y3;
    config.pin_d2 = SIM7670G_CAM_Y4;
    config.pin_d3 = SIM7670G_CAM_Y5;
    config.pin_d4 = SIM7670G_CAM_Y6;
    config.pin_d5 = SIM7670G_CAM_Y7;
    config.pin_d6 = SIM7670G_CAM_Y8;
    config.pin_d7 = SIM7670G_CAM_Y9;
    config.pin_xclk = SIM7670G_CAM_XCLK;
    config.pin_pclk = SIM7670G_CAM_PCLK;
    config.pin_vsync = SIM7670G_CAM_VSYNC;
    config.pin_href = SIM7670G_CAM_HREF;
    config.pin_sccb_sda = SIM7670G_CAM_SIOD;
    config.pin_sccb_scl = SIM7670G_CAM_SIOC;
    config.pin_pwdn = SIM7670G_CAM_PWDN;
    config.pin_reset = SIM7670G_CAM_RESET;
  } else if (board_type == BOARD_FREENOVE_S3) {
    config.pin_d0 = FREENOVE_CAM_Y2;
    config.pin_d1 = FREENOVE_CAM_Y3;
    config.pin_d2 = FREENOVE_CAM_Y4;
    config.pin_d3 = FREENOVE_CAM_Y5;
    config.pin_d4 = FREENOVE_CAM_Y6;
    config.pin_d5 = FREENOVE_CAM_Y7;
    config.pin_d6 = FREENOVE_CAM_Y8;
    config.pin_d7 = FREENOVE_CAM_Y9;
    config.pin_xclk = FREENOVE_CAM_XCLK;
    config.pin_pclk = FREENOVE_CAM_PCLK;
    config.pin_vsync = FREENOVE_CAM_VSYNC;
    config.pin_href = FREENOVE_CAM_HREF;
    config.pin_sccb_sda = FREENOVE_CAM_SIOD;
    config.pin_sccb_scl = FREENOVE_CAM_SIOC;
    config.pin_pwdn = FREENOVE_CAM_PWDN;
    config.pin_reset = FREENOVE_CAM_RESET;
    config.xclk_freq_hz = 20000000;  // 20MHz for Freenove
  } else {
    // Default to SIM7670G pins if unknown board type
    config.pin_d0 = CAM_PIN_Y2;
    config.pin_d1 = CAM_PIN_Y3;
    config.pin_d2 = CAM_PIN_Y4;
    config.pin_d3 = CAM_PIN_Y5;
    config.pin_d4 = CAM_PIN_Y6;
    config.pin_d5 = CAM_PIN_Y7;
    config.pin_d6 = CAM_PIN_Y8;
    config.pin_d7 = CAM_PIN_Y9;
    config.pin_xclk = CAM_PIN_XCLK;
    config.pin_pclk = CAM_PIN_PCLK;
    config.pin_vsync = CAM_PIN_VSYNC;
    config.pin_href = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_reset = CAM_PIN_RESET;
    config.xclk_freq_hz = 16000000;  // 16MHz default
  }

  if (board_type == BOARD_SIM7670G) {
    config.xclk_freq_hz = 16000000;  // 16MHz for Waveshare
  }

  config.pixel_format = PIXFORMAT_JPEG;

  // Conservative configuration to avoid DMA overflow and stack issues
  // Start with small frame buffer and work up
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.frame_size = FRAMESIZE_VGA;  // Start small: 640x480
  config.jpeg_quality = 15;  // Higher number = lower quality = less memory
  config.fb_count = 1;  // Single buffer to minimize memory

  if(psramFound()){
    Serial.println(F("PSRAM found - using conservative settings to avoid overflow"));
    Serial.printf("PSRAM size: %d, Free PSRAM: %d\n", ESP.getPsramSize(), ESP.getFreePsram());
    // Use moderate settings - not maximum to avoid DMA overflow
    config.frame_size = FRAMESIZE_SVGA;  // 800x600 (not UXGA to avoid overflow)
    config.jpeg_quality = 12;
    config.fb_count = 1;  // Stay with single buffer for stability
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  } else {
    Serial.println(F("ERROR: No PSRAM detected!"));
    Serial.println(F("This board REQUIRES PSRAM for camera operation"));
    if (board_type == BOARD_FREENOVE_S3) {
      Serial.println(F("Check Arduino IDE: Tools -> PSRAM -> OPI PSRAM"));
    } else {
      Serial.println(F("Check Arduino IDE: Tools -> PSRAM -> QSPI PSRAM"));
    }
    // Ultra-minimal settings without PSRAM
    config.frame_size = FRAMESIZE_QVGA;  // 320x240 - very small
    config.jpeg_quality = 20;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.print(F("Camera init failed: 0x"));
    Serial.println(err, HEX);
    addLog("ERROR", "Camera initialization failed: " + String(err, HEX));
    return false;
  }

  addLog("SUCCESS", "Camera initialized successfully");
  camera_initialized = true;
  return true;
}

String capturePhoto() {
  if (!camera_initialized || !camera_enabled) {
    addLog("WARNING", "Camera not initialized or disabled");
    return "";
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    addLog("ERROR", "Camera capture failed");
    return "";
  }

  String filename = "";
  if (sd_card_available) {
    Serial.println(F("Capturing photo to SD card..."));
    Serial.printf("Frame buffer size: %d bytes\n", fb->len);
    Serial.flush();

    // CRITICAL FIX: Camera operation disrupts SD_MMC peripheral
    // Re-initialize SD card connection after getting frame buffer
    Serial.println(F("Re-establishing SD card connection after camera capture..."));
    SD_MMC.end();
    delay(100);

    if (!SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_DATA)) {
      Serial.println(F("WARNING: setPins() failed during SD reinit"));
    }

    if (!SD_MMC.begin("/sdcard", true)) {
      Serial.println(F("ERROR: SD card reinit failed after camera capture"));
      addLog("ERROR", "SD card reinit failed after camera capture");
      sd_card_available = false;
      esp_camera_fb_return(fb);
      return "";
    }

    Serial.println(F("SD card re-initialized successfully"));

    // Generate filename with timestamp
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char fname[32];
      strftime(fname, sizeof(fname), "/%Y%m%d_%H%M%S.jpg", &timeinfo);
      filename = String(fname);

      Serial.print(F("Attempting to create file: "));
      Serial.println(filename);
      Serial.flush();

      // Check if SD card is still mounted
      uint8_t cardType = SD_MMC.cardType();
      if (cardType == CARD_NONE) {
        Serial.println(F("ERROR: SD card no longer detected!"));
        addLog("ERROR", "SD card was unmounted");
        sd_card_available = false;
        esp_camera_fb_return(fb);
        return "";
      }

      // Try to open file with FILE_WRITE mode
      Serial.println(F("Opening file with FILE_WRITE..."));
      File file = SD_MMC.open(filename.c_str(), FILE_WRITE);
      if (!file) {
        // Try alternative: open with "w" mode
        Serial.println(F("FILE_WRITE failed, trying with fopen mode..."));
        file = SD_MMC.open(filename.c_str(), "w");
      }

      if (file) {
        Serial.println(F("File opened successfully, writing data..."));
        size_t written = file.write(fb->buf, fb->len);
        file.close();
        Serial.printf("Wrote %d bytes successfully\n", written);
        addLog("SUCCESS", "Photo saved: " + filename + " (" + String(written) + " bytes)");
      } else {
        Serial.println(F("ERROR: Failed to open file on SD card"));
        Serial.println(F("Trying to create test file to diagnose..."));

        // Diagnostic test
        File test = SD_MMC.open("/test2.txt", FILE_WRITE);
        if (test) {
          test.println("Test after photo attempt");
          test.close();
          Serial.println(F("Test file created OK - photo filename may be invalid"));
          SD_MMC.remove("/test2.txt");
        } else {
          Serial.println(F("Test file also failed - SD card has become read-only"));
        }

        addLog("ERROR", "Failed to open file on SD card: " + filename);
        filename = "";
      }
    } else {
      Serial.println(F("ERROR: Could not get local time for filename"));
      addLog("ERROR", "Could not get time for photo filename");
    }
  } else {
    Serial.println(F("ERROR: SD card not available"));
    addLog("ERROR", "SD card not available for photo storage");
  }

  esp_camera_fb_return(fb);
  return filename;
}
#else
// Stub functions when camera library is not available
bool initCamera() {
  addLog("INFO", "Camera library not available");
  return false;
}

String capturePhoto() {
  return "";
}
#endif

// ========================================
// SD CARD FUNCTIONS
// ========================================

#ifdef HAS_CAMERA_LIB
bool initSDCard() {
  if (board_type == BOARD_GENERIC) {
    Serial.println(F("SD card not available - board type is generic"));
    return false;
  }

  Serial.println(F("Initializing SD card..."));

  // SD pins differ by board - set correct pins
  int sd_clk, sd_cmd, sd_data, sd_cd;
  if (board_type == BOARD_SIM7670G) {
    sd_clk = SIM7670G_SD_CLK;
    sd_cmd = SIM7670G_SD_CMD;
    sd_data = SIM7670G_SD_DATA;
    sd_cd = SIM7670G_SD_CD;
  } else if (board_type == BOARD_FREENOVE_S3) {
    sd_clk = FREENOVE_SD_CLK;
    sd_cmd = FREENOVE_SD_CMD;
    sd_data = FREENOVE_SD_DATA;
    sd_cd = FREENOVE_SD_CD;
  } else {
    // Default to SIM7670G pins
    sd_clk = SD_MMC_CLK;
    sd_cmd = SD_MMC_CMD;
    sd_data = SD_MMC_DATA;
    sd_cd = SD_CD_PIN;
  }

  // Check card detect pin if available
  if (sd_cd >= 0) {
    pinMode(sd_cd, INPUT_PULLUP);
    delay(100);
    int cardDetect = digitalRead(sd_cd);
    Serial.print(F("Card detect pin (GPIO "));
    Serial.print(sd_cd);
    Serial.print(F(") state: "));
    Serial.print(cardDetect);
    Serial.print(F(" ("));
    Serial.print(cardDetect == LOW ? "LOW - Card present?" : "HIGH - No card?");
    Serial.println(F(")"));
    Serial.println(F("NOTE: Card detect is optional - SD_MMC.begin() will try anyway"));
    Serial.flush();
  } else {
    Serial.println(F("No card detect pin - skipping card detect check"));
  }

  // Give SD card time to power up
  delay(500);

  // Try to initialize SD card in 1-bit mode
  Serial.println(F("Attempting SD_MMC.begin() in 1-bit mode..."));
  Serial.flush();

  // Try SD_MMC (SDMMC peripheral - faster but less compatible)
  Serial.println(F("Trying SD_MMC library..."));

  bool sdmmc_success = false;

  // Freenove uses default SDMMC pins, try without setPins first
  if (board_type == BOARD_FREENOVE_S3) {
    if (SD_MMC.begin("/sdcard", true)) {
      sdmmc_success = true;
      Serial.println(F("SD_MMC 1-bit mode SUCCESS with default pins"));
    }
  }

  // If not Freenove or default pins failed, try with explicit setPins
  if (!sdmmc_success) {
    if (!SD_MMC.setPins(sd_clk, sd_cmd, sd_data)) {
      Serial.println(F("WARNING: setPins() failed or not supported"));
    }
    Serial.flush();

    // Try 1-bit mode
    Serial.println(F("  Trying 1-bit mode..."));
    if (SD_MMC.begin("/sdcard", true)) {
      sdmmc_success = true;
      Serial.println(F("  SD_MMC 1-bit mode SUCCESS!"));
    } else {
      Serial.println(F("  1-bit mode failed, trying 4-bit..."));
      SD_MMC.end();
      delay(500);

      // Try 4-bit mode
      if (SD_MMC.begin("/sdcard", false)) {
        sdmmc_success = true;
        Serial.println(F("  SD_MMC 4-bit mode SUCCESS!"));
      } else {
        Serial.println(F("  SD_MMC failed in both modes"));
        SD_MMC.end();
      }
    }
  }

  // Method 2: If SD_MMC failed, try SPI mode with SD library
  if (!sdmmc_success) {
    Serial.println(F("Method 2: Trying SD library (SPI mode)..."));
    Serial.println(F("NOTE: SPI mode uses different pins - this may not work on this board"));
    Serial.flush();

    // Use default SPI pins or try to use the SDMMC pins as SPI
    // Note: This likely won't work as the hardware is designed for SDMMC mode
    if (SD.begin(SD_MMC_CMD)) {  // Use CMD pin as CS
      Serial.println(F("  SD SPI mode SUCCESS!"));
      sd_card_available = true;

      uint64_t cardSize = SD.cardSize() / (1024 * 1024);
      Serial.print(F("SD card (SPI mode) - Size: "));
      Serial.print((uint32_t)cardSize);
      Serial.println(F(" MB"));
      return true;
    } else {
      Serial.println(F("  SD SPI mode failed"));
      Serial.println(F(""));
      Serial.println(F("SD card initialization FAILED in all modes:"));
      Serial.println(F("  - SD_MMC 1-bit mode: FAILED"));
      Serial.println(F("  - SD_MMC 4-bit mode: FAILED"));
      Serial.println(F("  - SD SPI mode: FAILED"));
      Serial.println(F(""));
      Serial.println(F("This indicates a hardware issue with the SD slot"));
      sd_card_available = false;
      return false;
    }
  }

  Serial.println(F("SD_MMC.begin() succeeded!"));
  Serial.flush();

  uint8_t cardType = SD_MMC.cardType();
  Serial.print(F("Card type: "));
  Serial.println(cardType);
  Serial.flush();

  if (cardType == CARD_NONE) {
    Serial.println(F("No SD card attached"));
    Serial.println(F("System will continue without SD card"));
    SD_MMC.end();  // Clean up
    sd_card_available = false;
    return false;
  }

  // Get card size safely
  uint64_t cardSize = 0;
  if (cardType != CARD_NONE) {
    cardSize = SD_MMC.cardSize() / (1024 * 1024);
  }

  Serial.print(F("SD card initialized - Size: "));
  Serial.print((uint32_t)cardSize);
  Serial.println(F(" MB"));

  // Test write capability
  Serial.println(F("Testing SD card write capability..."));
  File testFile = SD_MMC.open("/test.txt", FILE_WRITE);
  if (testFile) {
    testFile.println("SD card write test");
    testFile.close();
    Serial.println(F("Write test SUCCESS - SD card is writable"));
    SD_MMC.remove("/test.txt");  // Clean up test file
    addLog("SUCCESS", "SD card initialized - Size: " + String((uint32_t)cardSize) + " MB");
    sd_card_available = true;
    return true;
  } else {
    Serial.println(F("Write test FAILED - SD card is READ-ONLY or write-protected"));
    Serial.println(F("Check: Physical write-protect switch on SD card"));
    addLog("ERROR", "SD card is read-only or write-protected");
    sd_card_available = false;
    return false;
  }
}
#else
// Stub function when camera library (with SD_MMC) is not available
bool initSDCard() {
  Serial.println(F("SD card library not available"));
  return false;
}
#endif

// ========================================
// CELLULAR MODEM FUNCTIONS
// ========================================

String sendATCommand(String command, unsigned long timeout = 1000) {
  if (ModemSerial == nullptr) return "ERROR: Modem not initialized";

  // Clear any pending data
  while (ModemSerial->available()) {
    ModemSerial->read();
  }

  // Send command character-by-character with delays (like Waveshare example)
  // This is CRITICAL for SIM7670G modem to properly receive commands
  for (size_t i = 0; i < command.length(); i++) {
    ModemSerial->write(command[i]);
    delay(10);
  }

  // Send line ending
  ModemSerial->write('\r');
  delay(10);
  ModemSerial->write('\n');
  delay(10);
  ModemSerial->flush();

  String response = "";
  unsigned long startTime = millis();
  bool gotResponse = false;

  while (millis() - startTime < timeout) {
    if (ModemSerial->available()) {
      char c = ModemSerial->read();
      response += c;
      gotResponse = true;
    }
    // Check for completion
    if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
      break;
    }
    // Small delay to allow data to arrive
    if (!gotResponse) {
      delay(10);
    }
  }

  // Debug output
  Serial.print(F("AT: "));
  Serial.print(command);
  Serial.print(F(" -> ["));
  Serial.print(response.length());
  Serial.print(F(" bytes] "));
  if (response.length() > 0) {
    // Print first 100 chars of response
    Serial.println(response.length() > 100 ? response.substring(0, 100) + "..." : response);
  } else {
    Serial.println(F("(empty)"));
  }

  return response;
}

bool initModem() {
  Serial.println(F("[MODEM] initModem() called"));
  Serial.flush();

  if (board_type != BOARD_SIM7670G) {
    Serial.println(F("[MODEM] Board is not SIM7670G, skipping"));
    Serial.flush();
    addLog("INFO", "Cellular modem not available - board type is generic");
    return false;
  }

  // Initialize modem serial object if not already done
  Serial.println(F("[MODEM] Creating HardwareSerial object..."));
  Serial.flush();
  if (ModemSerial == nullptr) {
    ModemSerial = new HardwareSerial(2);  // UART2
    Serial.println(F("[MODEM] HardwareSerial created"));
    Serial.flush();
  }

  // CRITICAL: Enable modem power (pin 33 must be HIGH) - like Waveshare example
  Serial.println(F("[MODEM] Enabling modem power on pin 33..."));
  Serial.flush();
  pinMode(MODEM_PWR_EN_PIN, OUTPUT);
  digitalWrite(MODEM_PWR_EN_PIN, HIGH);
  Serial.println(F("[MODEM] Pin 33 set HIGH - modem power enabled"));
  Serial.flush();

  // Initialize modem serial BEFORE power sequence
  // NOTE: In HardwareSerial, the parameter order is: baud, mode, rxPin, txPin (ESP32 perspective)
  // ESP32 RX=17 connects to Modem TX
  // ESP32 TX=18 connects to Modem RX
  // Our pin names are from MODEM perspective, so we need to swap them!
  Serial.printf("[MODEM] Initializing serial: ESP32 RX=17 (modem TX), ESP32 TX=18 (modem RX) at 115200 baud...\n");
  Serial.flush();
  ModemSerial->begin(115200, SERIAL_8N1, MODEM_TX_PIN, MODEM_RX_PIN);  // Swapped because pin names are from modem perspective!
  delay(100);
  Serial.println(F("[MODEM] Serial port opened"));
  Serial.flush();

  // Give modem time to power up after enabling pin 33
  Serial.println(F("[MODEM] Waiting for modem to power up..."));
  Serial.flush();
  delay(3000);  // Wait for modem to stabilize
  Serial.println(F("[MODEM] Power-up wait complete"));
  Serial.flush();

  // Test AT communication with multiple attempts and diagnostics
  Serial.println(F("[MODEM] Testing AT communication..."));
  Serial.flush();

  // First check if modem is sending anything
  Serial.println(F("[MODEM] Checking for any data from modem..."));
  delay(500);
  if (ModemSerial->available()) {
    Serial.print(F("[MODEM] Data available: "));
    while (ModemSerial->available()) {
      Serial.write(ModemSerial->read());
    }
    Serial.println();
  } else {
    Serial.println(F("[MODEM] No data from modem (yet)"));
  }
  Serial.flush();

  bool modemResponded = false;

  for (int i = 0; i < 5 && !modemResponded; i++) {
    Serial.printf("[MODEM] AT test attempt %d/5\n", i + 1);
    Serial.flush();

    String response = sendATCommand("AT", 2000);  // Longer timeout
    if (response.indexOf("OK") >= 0 || response.indexOf("AT") >= 0) {
      Serial.println(F("[MODEM] Modem responded!"));
      Serial.flush();
      addLog("SUCCESS", "Modem initialized");
      modemResponded = true;

      // Send AT again to stabilize
      sendATCommand("AT");
      delay(100);

      // Configure modem
      Serial.println(F("[MODEM] Configuring modem..."));
      Serial.flush();
      sendATCommand("ATE0");  // Echo off
      delay(100);
      sendATCommand("AT+CMEE=2");  // Verbose error messages
      delay(100);

      // Get modem information for diagnostics
      String modelResponse = sendATCommand("AT+CGMM");
      if (modelResponse.indexOf("OK") >= 0) {
        int start = modelResponse.indexOf("\n") + 1;
        int end = modelResponse.indexOf("\r", start);
        if (start > 0 && end > start) {
          modem_model = modelResponse.substring(start, end);
          modem_model.trim();
        }
      }

      String imeiResponse = sendATCommand("AT+CGSN");
      if (imeiResponse.indexOf("OK") >= 0) {
        int start = imeiResponse.indexOf("\n") + 1;
        int end = imeiResponse.indexOf("\r", start);
        if (start > 0 && end > start) {
          modem_imei = imeiResponse.substring(start, end);
          modem_imei.trim();
        }
      }

      String iccidResponse = sendATCommand("AT+CCID");
      if (iccidResponse.indexOf("+CCID:") >= 0) {
        int start = iccidResponse.indexOf(":") + 2;
        int end = iccidResponse.indexOf("\r", start);
        if (start > 1 && end > start) {
          modem_iccid = iccidResponse.substring(start, end);
          modem_iccid.trim();
        }
      }

      Serial.println(F("[MODEM] Configuration complete"));
      Serial.flush();
      modem_initialized = true;

      return true;
    }
    delay(1000);
  }

  // If normal configuration didn't work, try different baud rates
  if (!modemResponded) {
    Serial.println(F("[MODEM] Standard 115200 baud failed, trying other baud rates..."));
    Serial.flush();

    // Common modem baud rates to try
    uint32_t baudRates[] = {9600, 19200, 38400, 57600, 115200, 460800, 921600};

    for (int b = 0; b < 7 && !modemResponded; b++) {
      if (baudRates[b] == 115200) continue;  // Already tried this one

      Serial.printf("[MODEM] Trying baud rate: %d\n", baudRates[b]);
      ModemSerial->end();
      delay(100);
      ModemSerial->begin(baudRates[b], SERIAL_8N1, MODEM_TX_PIN, MODEM_RX_PIN);
      delay(500);

      for (int i = 0; i < 2 && !modemResponded; i++) {
        String response = sendATCommand("AT", 2000);
        if (response.indexOf("OK") >= 0 || response.indexOf("AT") >= 0) {
          Serial.printf("[MODEM] SUCCESS at %d baud!\n", baudRates[b]);
          addLog("SUCCESS", String("Modem initialized at ") + String(baudRates[b]) + " baud");
          modemResponded = true;
          break;
        }
        delay(500);
      }
    }
  }

  if (!modemResponded) {
    Serial.println(F("[MODEM] Failed to get AT response at any baud rate"));
    Serial.flush();
    addLog("ERROR", "Modem initialization failed - no response at any baud rate");
    return false;
  }

  // If we got here with modemResponded = true (from swapped pins), configure the modem
  Serial.println(F("[MODEM] Configuring modem..."));
  Serial.flush();

  sendATCommand("AT");
  delay(100);
  sendATCommand("ATE0");  // Echo off
  delay(100);
  sendATCommand("AT+CMEE=2");  // Verbose error messages
  delay(100);

  // Get modem information for diagnostics
  String modelResponse = sendATCommand("AT+CGMM");
  if (modelResponse.indexOf("OK") >= 0) {
    int start = modelResponse.indexOf("\n") + 1;
    int end = modelResponse.indexOf("\r", start);
    if (start > 0 && end > start) {
      modem_model = modelResponse.substring(start, end);
      modem_model.trim();
    }
  }

  String imeiResponse = sendATCommand("AT+CGSN");
  if (imeiResponse.indexOf("OK") >= 0) {
    int start = imeiResponse.indexOf("\n") + 1;
    int end = imeiResponse.indexOf("\r", start);
    if (start > 0 && end > start) {
      modem_imei = imeiResponse.substring(start, end);
      modem_imei.trim();
    }
  }

  String iccidResponse = sendATCommand("AT+CCID");
  if (iccidResponse.indexOf("+CCID:") >= 0) {
    int start = iccidResponse.indexOf(":") + 2;
    int end = iccidResponse.indexOf("\r", start);
    if (start > 1 && end > start) {
      modem_iccid = iccidResponse.substring(start, end);
      modem_iccid.trim();
    }
  }

  Serial.println(F("[MODEM] Configuration complete"));
  Serial.flush();
  modem_initialized = true;

  return true;
}

bool connectCellular() {
  if (!cellular_enabled || board_type != BOARD_SIM7670G) {
    return false;
  }

  Serial.println(F("Connecting to cellular network..."));
  addLog("INFO", "Connecting to cellular network");

  // Check SIM card - give modem extra time to detect it
  Serial.println(F("[CELLULAR] Checking SIM card status..."));
  delay(2000);  // Wait for SIM to be detected

  String response = sendATCommand("AT+CPIN?", 3000);

  // Print detailed SIM status
  Serial.print(F("[CELLULAR] SIM Status Response: "));
  Serial.println(response);

  if (response.indexOf("READY") >= 0) {
    Serial.println(F("[CELLULAR] SIM card is READY"));
  } else if (response.indexOf("SIM PIN") >= 0) {
    Serial.println(F("[CELLULAR] SIM card requires PIN"));
    addLog("ERROR", "SIM requires PIN code");
    return false;
  } else if (response.indexOf("SIM PUK") >= 0) {
    Serial.println(F("[CELLULAR] SIM card is PUK locked"));
    addLog("ERROR", "SIM is PUK locked");
    return false;
  } else if (response.indexOf("NOT INSERTED") >= 0 || response.indexOf("NOT READY") >= 0) {
    Serial.println(F("[CELLULAR] SIM card not detected"));
    addLog("ERROR", "No SIM card detected");
    return false;
  } else {
    Serial.println(F("[CELLULAR] Unknown SIM status - may not be ready yet"));
    addLog("ERROR", "SIM card not ready");
    return false;
  }

  // Wait for network registration
  for (int i = 0; i < 30; i++) {
    response = sendATCommand("AT+CREG?");
    if (response.indexOf("+CREG: 0,1") >= 0 || response.indexOf("+CREG: 0,5") >= 0) {
      Serial.println(F("Network registered!"));
      break;
    }
    delay(1000);
    esp_task_wdt_reset();
  }

  // Get operator name
  response = sendATCommand("AT+COPS?");
  int opStart = response.indexOf("\",\"") + 3;
  int opEnd = response.indexOf("\"", opStart);
  if (opStart > 2 && opEnd > opStart) {
    cellular_operator = response.substring(opStart, opEnd);
  }

  // Get signal strength
  response = sendATCommand("AT+CSQ");
  int rssiStart = response.indexOf(": ") + 2;
  int rssiEnd = response.indexOf(",", rssiStart);
  if (rssiStart > 1 && rssiEnd > rssiStart) {
    cellular_signal_strength = response.substring(rssiStart, rssiEnd).toInt();
  }

  // Get network type (2G/3G/4G)
  response = sendATCommand("AT+CPSI?");
  if (response.indexOf("LTE") >= 0) {
    modem_network_type = "4G LTE";
  } else if (response.indexOf("WCDMA") >= 0 || response.indexOf("HSDPA") >= 0) {
    modem_network_type = "3G";
  } else if (response.indexOf("GSM") >= 0 || response.indexOf("GPRS") >= 0) {
    modem_network_type = "2G";
  } else {
    modem_network_type = "Unknown";
  }

  // Configure APN
  if (apn.length() > 0) {
    sendATCommand("AT+CGDCONT=1,\"IP\",\"" + apn + "\"");
  }

  // Activate PDP context
  response = sendATCommand("AT+CGACT=1,1", 5000);
  if (response.indexOf("OK") >= 0) {
    cellular_connected = true;
    addLog("SUCCESS", "Cellular connected - " + cellular_operator + " (RSSI: " + String(cellular_signal_strength) + ")");
    Serial.println(F("Cellular network connected!"));
    return true;
  }

  addLog("ERROR", "Cellular connection failed");
  return false;
}

bool sendSMS(String phoneNumber, String message) {
  if (!cellular_connected) {
    addLog("ERROR", "SMS: No cellular connection");
    return false;
  }

  Serial.println(F("Sending SMS..."));

  // Set SMS text mode
  sendATCommand("AT+CMGF=1");

  // Set recipient
  ModemSerial->print("AT+CMGS=\"");
  ModemSerial->print(phoneNumber);
  ModemSerial->println("\"");
  delay(500);

  // Send message
  ModemSerial->print(message);
  ModemSerial->write(26);  // Ctrl+Z to send

  String response = "";
  unsigned long startTime = millis();
  while (millis() - startTime < 10000) {
    if (ModemSerial->available()) {
      response += (char)ModemSerial->read();
    }
    if (response.indexOf("OK") >= 0) {
      Serial.println(F("✓ SMS sent"));
      addLog("SUCCESS", "SMS sent to " + phoneNumber);
      return true;
    }
    if (response.indexOf("ERROR") >= 0) {
      break;
    }
  }

  Serial.println(F("✗ SMS failed"));
  addLog("ERROR", "SMS send failed");
  return false;
}

String sendHTTPRequest(String url, String method, String payload, String contentType) {
  if (!cellular_connected) {
    addLog("ERROR", "HTTP: No cellular connection");
    return "";
  }

  Serial.print(F("HTTP "));
  Serial.print(method);
  Serial.print(F(" via cellular: "));
  Serial.println(url);

  // Initialize HTTP
  sendATCommand("AT+HTTPTERM");  // Terminate any existing session
  delay(500);
  sendATCommand("AT+HTTPINIT");

  // Set parameters
  sendATCommand("AT+HTTPPARA=\"URL\",\"" + url + "\"");
  sendATCommand("AT+HTTPPARA=\"CONTENT\",\"" + contentType + "\"");

  int action = 0;  // GET
  if (method == "POST") {
    action = 1;
    // Send data
    sendATCommand("AT+HTTPDATA=" + String(payload.length()) + ",10000");
    delay(500);
    ModemSerial->print(payload);
    delay(1000);
  }

  // Execute request
  String response = sendATCommand("AT+HTTPACTION=" + String(action), 15000);
  delay(2000);

  // Read response
  response = sendATCommand("AT+HTTPREAD", 5000);

  // Terminate HTTP
  sendATCommand("AT+HTTPTERM");

  return response;
}

// ========================================
// GPS FUNCTIONS
// ========================================

#ifdef HAS_GPS_LIB
bool initGPS() {
  if (board_type != BOARD_SIM7670G || !cellular_enabled) {
    addLog("INFO", "GPS not available - board type is generic or modem not enabled");
    return false;
  }

  // Initialize GPS object if needed
  if (gps == nullptr) {
    gps = new TinyGPSPlus();
  }

  // Enable GNSS power
  sendATCommand("AT+CGNSPWR=1");
  addLog("INFO", "GPS initialized via modem");
  return true;
}

void updateGPS() {
  // GPS data is retrieved on-demand via AT commands
  // No continuous update needed
}

String getGPSLocation() {
  if (!gps_enabled || board_type != BOARD_SIM7670G || !cellular_enabled) {
    return "";
  }

  // Get GPS info from modem
  String response = sendATCommand("AT+CGNSINF");

  // Parse response: +CGNSINF: <GNSS run status>,<Fix status>,<UTC date & Time>,
  //                  <Latitude>,<Longitude>,<MSL Altitude>,<Speed Over Ground>,
  //                  <Course Over Ground>,<Fix Mode>,<Reserved1>,<HDOP>,<PDOP>,
  //                  <VDOP>,<Reserved2>,<GPS Satellites in View>,<GNSS Satellites Used>,
  //                  <GLONASS Satellites in View>,<Reserved3>,<C/N0 max>,<HPA>,<VPA>

  int infoStart = response.indexOf("+CGNSINF: ") + 10;
  if (infoStart < 10) return "GPS: No data";

  String info = response.substring(infoStart);
  int commas[25];
  int commaCount = 0;

  // Find all comma positions
  for (int i = 0; i < info.length() && commaCount < 25; i++) {
    if (info[i] == ',') {
      commas[commaCount++] = i;
    }
  }

  if (commaCount < 14) return "GPS: Parse error";

  // Extract fix status (field 1)
  String fixStatus = info.substring(commas[0] + 1, commas[1]);
  if (fixStatus != "1") {
    return "GPS: No fix";
  }

  // Extract latitude (field 3)
  String lat = info.substring(commas[2] + 1, commas[3]);
  // Extract longitude (field 4)
  String lon = info.substring(commas[3] + 1, commas[4]);
  // Extract altitude (field 5)
  String alt = info.substring(commas[4] + 1, commas[5]);
  // Extract satellites used (field 15)
  String sats = info.substring(commas[14] + 1, commas[15]);

  if (lat.length() == 0 || lon.length() == 0) {
    return "GPS: No fix";
  }

  String loc = "Lat: " + lat + ", Lon: " + lon;
  if (alt.length() > 0 && alt != "0") {
    loc += ", Alt: " + alt + "m";
  }
  if (sats.length() > 0) {
    loc += " (" + sats + " sats)";
  }

  return loc;
}
#else
// Stub functions when GPS library is not available
bool initGPS() {
  addLog("INFO", "GPS library not available");
  return false;
}

void updateGPS() {
  // No-op
}

String getGPSLocation() {
  return "";
}
#endif

// ========================================
// BMP180 SENSOR FUNCTIONS
// ========================================

#ifdef HAS_BMP180_LIB
bool initBMP180() {
  if (!bmp180_enabled) {
    addLog("INFO", "BMP180 sensor disabled");
    return false;
  }

  Serial.println(F("Initializing BMP180 sensor..."));
  Serial.printf("I2C pins - SDA: GPIO%d, SCL: GPIO%d\n", bmp180_sda_pin, bmp180_scl_pin);

  // End existing Wire instance to properly reinitialize with new pins
  Wire.end();
  delay(100);

  // Enable internal pull-ups on I2C pins (helps if external pull-ups are missing)
  pinMode(bmp180_sda_pin, INPUT_PULLUP);
  pinMode(bmp180_scl_pin, INPUT_PULLUP);
  delay(50);

  // Initialize I2C with custom pins
  bool wireStarted = Wire.begin(bmp180_sda_pin, bmp180_scl_pin);
  if (!wireStarted) {
    Serial.println(F("ERROR: Failed to initialize I2C bus!"));
    Serial.println(F("       This usually means invalid pin numbers"));
    addLog("ERROR", "I2C initialization failed - check pins");
    bmp180_initialized = false;
    return false;
  }
  Serial.println(F("  ✓ I2C bus initialized"));
  delay(100);

  // Try multiple I2C speeds for better compatibility
  Serial.println(F("  Setting I2C clock to 100kHz..."));
  Wire.setClock(100000);  // Start with 100kHz for reliability
  delay(100);

  // Perform a quick I2C bus scan to verify the sensor is present
  Serial.println(F("  Scanning I2C bus for device at 0x77..."));
  Wire.beginTransmission(0x77);
  uint8_t error = Wire.endTransmission();
  if (error != 0) {
    Serial.printf("  ✗ No response at 0x77 (error code: %d)\n", error);
    Serial.println(F("\n  Scanning entire I2C bus for any devices..."));
    bool foundAny = false;
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("  Found device at 0x%02X\n", addr);
        foundAny = true;
      }
    }
    if (!foundAny) {
      Serial.println(F("  ✗ No I2C devices found on the bus!"));
      Serial.println(F("\n  CRITICAL: This indicates a wiring problem:"));
      Serial.println(F("    - Check ALL connections are secure"));
      Serial.println(F("    - Verify you're using 3.3V (NOT 5V!)"));
      Serial.println(F("    - Try different GPIO pins"));
      Serial.println(F("    - Check for damaged wires/breadboard"));
      Serial.println(F("    - Module might be defective"));
    }
  } else {
    Serial.println(F("  ✓ Device detected at 0x77!"));
  }
  delay(50);

  // Initialize BMP180 object if needed
  if (bmp == nullptr) {
    bmp = new Adafruit_BMP085();
  }

  // Try to initialize the sensor with custom Wire instance
  Serial.println(F("  Attempting to detect BMP180 at 0x77..."));

  // The Adafruit library's begin() method can take a Wire instance
  // Try begin() with Wire parameter (if library supports it)
  bool sensorFound = false;

  // Method 1: Try with Wire parameter (newer library versions)
  #if defined(ARDUINO_ARCH_ESP32)
    // ESP32-specific: Use TwoWire parameter
    sensorFound = bmp->begin(BMP085_STANDARD, &Wire);
  #else
    // Fallback for older library versions
    sensorFound = bmp->begin();
  #endif

  if (!sensorFound) {
    // Try alternative initialization mode
    Serial.println(F("  ✗ Standard mode failed, trying ultra-high resolution mode..."));
    sensorFound = bmp->begin(BMP085_ULTRAHIGHRES, &Wire);
  }

  if (!sensorFound) {
    Serial.println(F("\n  ✗✗✗ ERROR: Could not find BMP180 sensor! ✗✗✗"));
    Serial.println(F("\n  This means the sensor is NOT responding at 0x77."));
    Serial.println(F("\n  POSSIBLE CAUSES:"));
    Serial.println(F("  1. Wrong sensor type (you have BME280/BMP280, not BMP180)"));
    Serial.println(F("  2. Defective sensor module"));
    Serial.println(F("  3. Wiring issue:"));
    Serial.printf("     - VCC -> 3.3V\n");
    Serial.printf("     - GND -> GND\n");
    Serial.printf("     - SDA -> GPIO%d\n", bmp180_sda_pin);
    Serial.printf("     - SCL -> GPIO%d\n", bmp180_scl_pin);
    Serial.println(F("\n  NEXT STEPS:"));
    Serial.println(F("  1. Click 'Search for BMP180' button in web interface"));
    Serial.println(F("  2. Copy the scan results"));
    Serial.println(F("  3. Try swapping SDA and SCL wires"));
    Serial.println(F("  4. Try different GPIO pins (21/22)"));
    addLog("ERROR", "BMP180 sensor not found - check wiring");
    bmp180_initialized = false;
    return false;
  }

  Serial.println(F("✓ BMP180 sensor initialized successfully"));
  addLog("SUCCESS", "BMP180 sensor initialized");
  bmp180_initialized = true;

  // Read initial values
  updateBMP180();

  return true;
}

void updateBMP180() {
  if (!bmp180_initialized || !bmp180_enabled || bmp == nullptr) {
    return;
  }

  // Read temperature in Celsius
  last_temperature = bmp->readTemperature();

  // Read pressure in Pascals (convert to hPa/mbar)
  last_pressure = bmp->readPressure() / 100.0;

  // Calculate altitude (meters) based on standard atmospheric pressure
  last_altitude = bmp->readAltitude();

  // Optional: Print to serial for debugging
  // Serial.printf("BMP180 - Temp: %.1f°C, Pressure: %.1f hPa, Alt: %.1f m\n",
  //               last_temperature, last_pressure, last_altitude);
}

String getBMP180Data() {
  if (!bmp180_initialized || !bmp180_enabled) {
    return "";
  }

  // Update readings
  updateBMP180();

  String data = "";
  data += "Temperature: " + String(last_temperature, 1) + "°C";
  data += ", Pressure: " + String(last_pressure, 1) + " hPa";
  data += ", Altitude: " + String(last_altitude, 1) + " m";

  return data;
}

String getBMP180DataFormatted() {
  if (!bmp180_initialized || !bmp180_enabled) {
    return "";
  }

  // Update readings
  updateBMP180();

  String data = "\n🌡️  Temperature: " + String(last_temperature, 1) + "°C / " + String(last_temperature * 9.0 / 5.0 + 32.0, 1) + "°F";
  data += "\n🔽 Pressure: " + String(last_pressure, 1) + " hPa / " + String(last_pressure * 0.02953, 2) + " inHg";
  data += "\n⛰️  Altitude: " + String(last_altitude, 1) + " m / " + String(last_altitude * 3.28084, 1) + " ft";

  return data;
}

#else
// Stub functions when BMP180 library is not available
bool initBMP180() {
  addLog("INFO", "BMP180 library not available");
  return false;
}

void updateBMP180() {
  // No-op
}

String getBMP180Data() {
  return "";
}

String getBMP180DataFormatted() {
  return "";
}
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);  // Give serial time to initialize
  Serial.println(F("\n\n=== ESP32-Notifier v" VERSION " ==="));
  Serial.println(F("[BOOT] Serial initialized"));
  Serial.flush();

  // Print memory before anything else
  Serial.printf("[BOOT] Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("[BOOT] Chip: %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.flush();

  // Enable watchdog timer with compatibility for different ESP32 core versions
  Serial.println(F("[BOOT] Initializing watchdog..."));
  Serial.flush();
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    // ESP32 Arduino Core 3.x
    esp_task_wdt_deinit();  // Deinitialize if already initialized
    esp_task_wdt_config_t wdt_config = {
      .timeout_ms = WDT_TIMEOUT * 1000,
      .idle_core_mask = 0,
      .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);
  #else
    // ESP32 Arduino Core 2.x
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);
  #endif
  Serial.println(F("[BOOT] Watchdog timer enabled (30s)"));
  Serial.flush();

  Serial.println(F("[BOOT] Loading preferences..."));
  Serial.flush();
  loadPreferences();
  Serial.println(F("[BOOT] Preferences loaded OK"));
  Serial.flush();
  delay(100);

  // Initialize board-specific features
  Serial.println(F("[BOOT] Checking board configuration..."));
  Serial.flush();
  delay(100);

  Serial.print(F("[BOOT] Board type: "));
  Serial.print(board_type);
  Serial.println(F(" (0=Generic, 1=SIM7670G)"));
  Serial.flush();
  delay(100);

  Serial.print(F("[BOOT] Cellular enabled: "));
  Serial.print(cellular_enabled);
  Serial.print(F(", Camera: "));
  Serial.print(camera_enabled);
  Serial.print(F(", GPS: "));
  Serial.println(gps_enabled);
  Serial.flush();
  delay(100);

  if (board_type == BOARD_SIM7670G) {
    Serial.println(F("[BOOT] Board: ESP32-S3-SIM7670G-4G (Waveshare)"));
    Serial.flush();
    addLog("INFO", "Board type: ESP32-S3-SIM7670G-4G (Waveshare)");

    // Initialize cellular modem first if enabled
    // Uses UART on pins 17 (TX) and 18 (RX) based on Waveshare examples
    if (cellular_enabled) {
      Serial.println(F("[BOOT] Initializing cellular modem..."));
      Serial.flush();
      esp_task_wdt_reset();  // Reset watchdog before modem init

      if (initModem()) {
        Serial.println(F("[BOOT] Modem initialized successfully"));
        Serial.flush();

        // Try to connect to cellular network
        if (connectCellular()) {
          Serial.println(F("[BOOT] Cellular network connected"));
          Serial.flush();
        } else {
          Serial.println(F("[BOOT] Cellular connection failed - continuing without cellular"));
          Serial.flush();
        }
      } else {
        Serial.println(F("[BOOT] Modem initialization failed - continuing without cellular"));
        Serial.flush();
      }
    } else {
      Serial.println(F("[BOOT] Cellular disabled, skipping modem"));
      Serial.flush();
    }

    // Initialize SD card
    Serial.println(F("[BOOT] Initializing SD card..."));
    Serial.flush();
    esp_task_wdt_reset();  // Reset watchdog before SD card init
    if (initSDCard()) {
      Serial.println(F("[BOOT] SD card ready"));
      Serial.flush();
    } else {
      Serial.println(F("[BOOT] SD card init failed - continuing without SD"));
      Serial.flush();
    }

    // Initialize camera if enabled
    if (camera_enabled) {
      Serial.println(F("[BOOT] Initializing camera..."));
      Serial.flush();
      esp_task_wdt_reset();  // Reset watchdog before camera init
      if (initCamera()) {
        Serial.println(F("[BOOT] Camera ready"));
        Serial.flush();

        // Camera initialization may affect SD_MMC - verify SD card is still accessible
        Serial.println(F("[BOOT] Verifying SD card after camera init..."));
        uint8_t cardType = SD_MMC.cardType();
        if (cardType == CARD_NONE) {
          Serial.println(F("[BOOT] WARNING: SD card unmounted after camera init"));
          Serial.println(F("[BOOT] Reinitializing SD card..."));
          // Try to reinit SD card
          if (initSDCard()) {
            Serial.println(F("[BOOT] SD card reinitialized successfully"));
          } else {
            Serial.println(F("[BOOT] SD card reinit failed - camera and SD may conflict"));
          }
        } else {
          Serial.println(F("[BOOT] SD card still accessible after camera init"));
        }
      } else {
        Serial.println(F("[BOOT] Camera init failed"));
        Serial.flush();
      }
    } else {
      Serial.println(F("[BOOT] Camera disabled, skipping"));
      Serial.flush();
    }

    // Initialize GPS if enabled (requires modem)
    if (gps_enabled && cellular_enabled) {
      Serial.println(F("[BOOT] Initializing GPS..."));
      Serial.flush();
      esp_task_wdt_reset();  // Reset watchdog before GPS init
      if (initGPS()) {
        Serial.println(F("[BOOT] GPS ready"));
        Serial.flush();
      } else {
        Serial.println(F("[BOOT] GPS init failed"));
        Serial.flush();
      }
    } else {
      Serial.println(F("[BOOT] GPS disabled or no cellular, skipping"));
      Serial.flush();
    }
  } else if (board_type == BOARD_FREENOVE_S3) {
    Serial.println(F("[BOOT] Board: Freenove ESP32-S3 CAM"));
    Serial.flush();
    addLog("INFO", "Board type: Freenove ESP32-S3 CAM");

    // Freenove board has camera and SD card but no cellular modem
    // Initialize SD card if camera is enabled
    if (camera_enabled) {
      Serial.println(F("[BOOT] Initializing SD card..."));
      Serial.flush();
      esp_task_wdt_reset();  // Reset watchdog
      if (initSDCard()) {
        Serial.println(F("[BOOT] SD card mounted"));
        Serial.flush();
      } else {
        Serial.println(F("[BOOT] SD card init failed - continuing without SD"));
        Serial.flush();
      }

      // Initialize camera
      Serial.println(F("[BOOT] Initializing camera..."));
      Serial.flush();
      esp_task_wdt_reset();  // Reset watchdog before camera init
      if (initCamera()) {
        Serial.println(F("[BOOT] Camera ready"));
        Serial.flush();

        // Verify SD card after camera init
        #ifdef HAS_CAMERA_LIB
        Serial.println(F("[BOOT] Verifying SD card after camera init..."));
        uint8_t cardType = SD_MMC.cardType();
        if (cardType == CARD_NONE) {
          Serial.println(F("[BOOT] WARNING: SD card unmounted after camera init"));
          Serial.println(F("[BOOT] Reinitializing SD card..."));
          if (initSDCard()) {
            Serial.println(F("[BOOT] SD card reinitialized successfully"));
          } else {
            Serial.println(F("[BOOT] SD card reinit failed"));
          }
        } else {
          Serial.println(F("[BOOT] SD card still accessible after camera init"));
        }
        #endif
      } else {
        Serial.println(F("[BOOT] Camera init failed"));
        Serial.flush();
      }
    } else {
      Serial.println(F("[BOOT] Camera disabled, skipping"));
      Serial.flush();
    }
  } else {
    Serial.println(F("[BOOT] Board: Generic ESP32-S3"));
    Serial.flush();
    addLog("INFO", "Board type: Generic ESP32-S3");
  }

  // Initialize BMP180 sensor if enabled (works on all board types)
  Serial.println(F("========================================"));
  Serial.println(F("[BOOT] BMP180 SENSOR INITIALIZATION"));
  Serial.println(F("========================================"));
  if (bmp180_enabled) {
    Serial.println(F("[BOOT] BMP180 sensor is ENABLED"));
    Serial.printf("[BOOT] I2C Configuration - SDA: GPIO%d, SCL: GPIO%d\n", bmp180_sda_pin, bmp180_scl_pin);
    Serial.println(F("[BOOT] Attempting to initialize BMP180..."));
    Serial.flush();
    esp_task_wdt_reset();  // Reset watchdog before sensor init
    if (initBMP180()) {
      Serial.println(F("[BOOT] ✓ BMP180 sensor initialized successfully!"));
      Serial.printf("[BOOT] Temperature: %.1f°C, Pressure: %.1f hPa\n", last_temperature, last_pressure);
      Serial.flush();
    } else {
      Serial.println(F("[BOOT] ✗ BMP180 sensor initialization FAILED"));
      Serial.println(F("[BOOT] Check the following:"));
      Serial.println(F("[BOOT]   1. Wiring: VCC->3.3V, GND->GND"));
      Serial.printf("[BOOT]   2. SDA connected to GPIO%d\n", bmp180_sda_pin);
      Serial.printf("[BOOT]   3. SCL connected to GPIO%d\n", bmp180_scl_pin);
      Serial.println(F("[BOOT]   4. Use web interface 'Search for BMP180' to scan I2C bus"));
      Serial.println(F("[BOOT] Continuing without BMP180 sensor..."));
      Serial.flush();
    }
  } else {
    Serial.println(F("[BOOT] BMP180 sensor is DISABLED in settings"));
    Serial.println(F("[BOOT] Enable it in the web interface to use the sensor"));
    Serial.flush();
  }
  Serial.println(F("========================================"));
  Serial.flush();

  // Configure enabled input pins
  Serial.println(F("[BOOT] Configuring input pins..."));
  Serial.flush();
  for (int i = 0; i < MAX_INPUTS; i++) {
    if (inputs[i].enabled) {
      pinMode(inputs[i].pin, INPUT_PULLDOWN);
      inputs[i].lastState = digitalRead(inputs[i].pin);
      Serial.print(F("[BOOT] Input "));
      Serial.print(i + 1);
      Serial.print(F(" ("));
      Serial.print(inputs[i].name);
      Serial.print(F(") on pin "));
      Serial.print(inputs[i].pin);
      Serial.print(F(": "));
      Serial.println(inputs[i].lastState ? F("HIGH") : F("LOW"));
      Serial.flush();
    }
  }

  // Check if WiFi credentials are configured
  Serial.println(F("[BOOT] Checking WiFi configuration..."));
  Serial.flush();
  if (wifi_ssid.length() == 0) {
    Serial.println(F("[BOOT] No WiFi credentials, starting AP mode"));
    Serial.flush();
    startAccessPoint();
    addLog("INFO", "Started in AP mode - no WiFi credentials");
  } else {
    Serial.printf("[BOOT] WiFi SSID configured: %s\n", wifi_ssid.c_str());
    Serial.flush();
    connectWiFiNonBlocking();
  }

  Serial.println(F("[BOOT] Setting up web server..."));
  Serial.flush();
  setupWebServer();
  server.begin();
  Serial.println(F("[BOOT] Web server started"));
  Serial.flush();

  Serial.printf("[BOOT] Free Heap at end: %d bytes\n", ESP.getFreeHeap());
  Serial.flush();
  addLog("INFO", "System started - ESP32-Notifier v" VERSION);
  Serial.println(F("[BOOT] === Setup complete ==="));
  Serial.flush();
}

void loop() {
  esp_task_wdt_reset();

  // Always handle web server (needed for AP mode)
  server.handleClient();

  // Handle deferred operations (after HTTP response sent)
  // IMPORTANT: Save preferences FIRST before any hardware operations
  // that might block or fail, to ensure settings are persisted
  if (needs_save_prefs) {
    needs_save_prefs = false;
    delay(50);  // Brief delay to ensure HTTP response fully sent
    addLog("INFO", "Saving preferences to flash");
    savePreferences();
  }

  if (needs_time_update) {
    needs_time_update = false;
    delay(50);  // Brief delay to ensure HTTP response fully sent
    addLog("INFO", "Updating time configuration");
    configTime(pending_gmt_offset, pending_daylight_offset, ntpServer);
  }

  if (bmp180_needs_reinit) {
    bmp180_needs_reinit = false;
    delay(100);  // Brief delay to ensure HTTP response fully sent
    Serial.println(F("\n========================================"));
    Serial.println(F("[REINIT] Reinitializing BMP180 sensor..."));
    Serial.printf("[REINIT] New I2C pins - SDA: GPIO%d, SCL: GPIO%d\n", bmp180_sda_pin, bmp180_scl_pin);
    Serial.println(F("========================================"));
    Serial.flush();
    if (initBMP180()) {
      Serial.println(F("[REINIT] ✓ BMP180 reinitialization successful!"));
    } else {
      Serial.println(F("[REINIT] ✗ BMP180 reinitialization failed!"));
      Serial.println(F("[REINIT] Use 'Search for BMP180' in web interface to scan I2C bus"));
    }
    Serial.println(F("========================================\n"));
    Serial.flush();
  }

  if (apMode) {
    // In AP mode, just handle web requests for configuration
    return;
  }

  updateWiFiConnection();

  if (wifiState == WIFI_CONNECTED) {
    if (!timeIsSynced && millis() - lastTimeSyncAttempt > TIME_SYNC_RETRY) {
      attemptTimeSync();
    }

    checkAndReconnectWiFi();
    processRetryQueue();
  }

  // Update GPS data if enabled
  if (gps_enabled && board_type == BOARD_SIM7670G) {
    updateGPS();
  }

  // Update BMP180 sensor data if enabled
  // Read sensor every loop (sensor library handles timing internally)
  if (bmp180_enabled && bmp180_initialized) {
    updateBMP180();
  }

  // Monitor all enabled inputs
  for (int i = 0; i < MAX_INPUTS; i++) {
    if (inputs[i].enabled) {
      monitorInput(i);
    }
  }
}

void monitorInput(int index) {
  InputConfig &input = inputs[index];
  bool reading = digitalRead(input.pin);

  if (reading != input.currentState) {
    Serial.print(F("[INPUT] Pin "));
    Serial.print(input.pin);
    Serial.print(F(" changed to "));
    Serial.println(reading ? "HIGH" : "LOW");
    input.lastDebounceTime = millis();
    input.currentState = reading;
  }

  if ((millis() - input.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (input.currentState != input.lastState) {
      Serial.print(F("[INPUT] State confirmed for "));
      Serial.print(input.name);
      Serial.print(F(" (pin "));
      Serial.print(input.pin);
      Serial.print(F("): "));
      Serial.println(input.currentState ? "HIGH" : "LOW");
      input.lastState = input.currentState;

      bool shouldNotify = false;
      String message;

      if (input.momentary_mode) {
        if (input.currentState == HIGH) {
          shouldNotify = true;
          message = input.message_on;
          Serial.print(input.name);
          Serial.println(F(" - Momentary trigger (HIGH)"));
        }
      } else {
        shouldNotify = true;
        message = input.currentState == HIGH ? input.message_on : input.message_off;
      }

      if (shouldNotify) {
        // Rate limiting
        if (millis() - input.lastNotificationTime >= MIN_NOTIFICATION_INTERVAL) {
          String timestamp = getFormattedTime();
          message.replace("{timestamp}", timestamp);

          // Capture photo if enabled for this input
          String photoFilename = "";
          if (input.capture_photo && camera_enabled && camera_initialized) {
            photoFilename = capturePhoto();
            if (photoFilename.length() > 0) {
              Serial.print(F("Photo captured: "));
              Serial.println(photoFilename);
            }
          }

          // Add GPS location if enabled for this input
          if (input.include_gps && gps_enabled) {
            String location = getGPSLocation();
            if (location.length() > 0) {
              message += "\n" + location;
            }
          }

          Serial.print(input.name);
          Serial.print(F(" state changed: "));
          Serial.println(message);

          addLog("INFO", "Trigger: " + input.name + " - " + (input.currentState == HIGH ? "HIGH" : "LOW"));
          sendNotifications(notification_title, message, photoFilename);
          input.lastNotificationTime = millis();
        } else {
          Serial.print(input.name);
          Serial.println(F(" - Notification rate limited"));
          addLog("WARNING", input.name + " notification rate limited");
        }
      }
    }
  }
}

void startAccessPoint() {
  Serial.println(F("Starting Access Point mode..."));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress IP = WiFi.softAPIP();
  Serial.print(F("AP IP address: "));
  Serial.println(IP);
  Serial.print(F("AP SSID: "));
  Serial.println(AP_SSID);
  Serial.print(F("AP Password: "));
  Serial.println(AP_PASSWORD);
  apMode = true;
  wifiState = WIFI_CONNECTED;
}

void connectWiFiNonBlocking() {
  Serial.print(F("Connecting to WiFi: "));
  Serial.println(wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  wifiState = WIFI_CONNECTING;
  wifiConnectStart = millis();
  apMode = false;
}

void updateWiFiConnection() {
  if (apMode) return;  // Don't update WiFi state in AP mode
  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiState = WIFI_CONNECTED;
      Serial.println(F("\nWiFi connected!"));
      Serial.print(F("IP address: "));
      Serial.println(WiFi.localIP());
      addLog("SUCCESS", "WiFi connected - IP: " + WiFi.localIP().toString());
      attemptTimeSync();
    } else if (millis() - wifiConnectStart > WIFI_TIMEOUT) {
      wifiState = WIFI_FAILED;
      Serial.println(F("\nWiFi connection failed!"));
      addLog("ERROR", "WiFi connection timeout - falling back to AP mode");

      // Fall back to Access Point mode so user can reconfigure
      Serial.println(F("Falling back to Access Point mode..."));
      WiFi.disconnect();
      startAccessPoint();
    }
  }
}

void checkAndReconnectWiFi() {
  if (apMode) return;  // Don't try to reconnect in AP mode
  if (millis() - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
  lastWiFiCheck = millis();

  if (WiFi.status() != WL_CONNECTED && !wifiReconnecting) {
    wifiReconnecting = true;
    Serial.println(F("WiFi disconnected! Reconnecting..."));
    addLog("WARNING", "WiFi disconnected - attempting reconnection");

    WiFi.disconnect();
    connectWiFiNonBlocking();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(F("."));
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("\nWiFi reconnected!"));
      addLog("SUCCESS", "WiFi reconnected successfully");
      configTime(gmt_offset, daylight_offset, ntpServer);
    } else {
      Serial.println(F("\nWiFi reconnection failed"));
    }

    wifiReconnecting = false;
  }
}

void attemptTimeSync() {
  lastTimeSyncAttempt = millis();
  configTime(gmt_offset, daylight_offset, ntpServer);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    timeIsSynced = true;
    Serial.println(F("Time synchronized!"));
  }
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return F("Time unavailable");
  }

  char timeString[50];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeString);
}

void loadPreferences() {
  Serial.println(F("[PREF] Opening preferences namespace..."));
  Serial.flush();
  preferences.begin("notifier", false);
  Serial.println(F("[PREF] Namespace opened"));
  Serial.flush();

  Serial.println(F("[PREF] Loading WiFi credentials..."));
  Serial.flush();
  wifi_ssid = preferences.getString(PrefKeys::WIFI_SSID, wifi_ssid);
  wifi_password = preferences.getString(PrefKeys::WIFI_PASS, wifi_password);
  web_username = preferences.getString(PrefKeys::WEB_USER, web_username);
  web_password = preferences.getString(PrefKeys::WEB_PASS, web_password);
  Serial.println(F("[PREF] WiFi credentials loaded"));
  Serial.flush();

  Serial.println(F("[PREF] Loading Pushbullet config..."));
  Serial.flush();
  pushbullet_token = preferences.getString(PrefKeys::PB_TOKEN, pushbullet_token);
  pushbullet_enabled = preferences.getBool(PrefKeys::PB_ENABLED, pushbullet_enabled);

  Serial.println(F("[PREF] Loading email config..."));
  Serial.flush();
  smtp_host = preferences.getString(PrefKeys::SMTP_HOST, smtp_host);
  smtp_port = preferences.getInt(PrefKeys::SMTP_PORT, smtp_port);
  smtp_email = preferences.getString(PrefKeys::SMTP_EMAIL, smtp_email);
  smtp_password = preferences.getString(PrefKeys::SMTP_PASS, smtp_password);
  recipient_email = preferences.getString(PrefKeys::RCPT_EMAIL, recipient_email);
  email_enabled = preferences.getBool(PrefKeys::EMAIL_ENABLED, email_enabled);

  Serial.println(F("[PREF] Loading Telegram config..."));
  Serial.flush();
  telegram_token = preferences.getString(PrefKeys::TG_TOKEN, telegram_token);
  telegram_chat_id = preferences.getString(PrefKeys::TG_CHAT, telegram_chat_id);
  telegram_enabled = preferences.getBool(PrefKeys::TG_ENABLED, telegram_enabled);

  Serial.println(F("[PREF] Loading notification settings..."));
  Serial.flush();
  notification_title = preferences.getString(PrefKeys::NOTIF_TITLE, notification_title);
  gmt_offset = preferences.getLong(PrefKeys::GMT_OFFSET, gmt_offset);
  daylight_offset = preferences.getInt(PrefKeys::DAY_OFFSET, daylight_offset);

  Serial.println(F("[PREF] Loading board configuration..."));
  Serial.flush();
  // NEW: Load board configuration
  board_type = preferences.getInt(PrefKeys::BOARD_TYPE, board_type);
  Serial.print(F("[PREF] Board type loaded: "));
  Serial.println(board_type);
  Serial.flush();

  camera_enabled = preferences.getBool(PrefKeys::CAMERA_EN, camera_enabled);
  gps_enabled = preferences.getBool(PrefKeys::GPS_EN, gps_enabled);

  Serial.println(F("[PREF] Loading cellular configuration..."));
  Serial.flush();
  // NEW: v3.1 Load cellular configuration
  cellular_enabled = preferences.getBool(PrefKeys::CELL_EN, cellular_enabled);
  apn = preferences.getString(PrefKeys::APN, apn);
  pushbullet_conn_mode = preferences.getInt(PrefKeys::PB_CONN_MODE, pushbullet_conn_mode);
  email_conn_mode = preferences.getInt(PrefKeys::EMAIL_CONN_MODE, email_conn_mode);
  telegram_conn_mode = preferences.getInt(PrefKeys::TG_CONN_MODE, telegram_conn_mode);
  sms_conn_mode = preferences.getInt(PrefKeys::SMS_CONN_MODE, sms_conn_mode);
  sms_enabled = preferences.getBool(PrefKeys::SMS_EN, sms_enabled);
  sms_phone_number = preferences.getString(PrefKeys::SMS_PHONE, sms_phone_number);

  Serial.println(F("[PREF] Loading BMP180 sensor configuration..."));
  Serial.flush();
  // Load BMP180 sensor configuration
  bmp180_enabled = preferences.getBool(PrefKeys::BMP180_EN, bmp180_enabled);
  bmp180_sda_pin = preferences.getInt(PrefKeys::BMP180_SDA, bmp180_sda_pin);
  bmp180_scl_pin = preferences.getInt(PrefKeys::BMP180_SCL, bmp180_scl_pin);
  include_bmp180_in_notifications = preferences.getBool(PrefKeys::BMP180_IN_NOTIF, include_bmp180_in_notifications);
  Serial.printf("[PREF] BMP180 - Enabled: %s, SDA: GPIO%d, SCL: GPIO%d, Include in notifications: %s\n",
                bmp180_enabled ? "YES" : "NO",
                bmp180_sda_pin,
                bmp180_scl_pin,
                include_bmp180_in_notifications ? "YES" : "NO");
  Serial.flush();

  // Update input pin defaults based on board type before loading preferences
  // This ensures SIM7670G board uses safe pins that don't conflict with peripherals
  if (board_type == BOARD_SIM7670G) {
    // Safe pins for SIM7670G: 21, 38, 39, 40 (avoid modem/camera/SD/strapping pins)
    int safe_pins[MAX_INPUTS] = {21, 38, 39, 40};
    for (int i = 0; i < MAX_INPUTS; i++) {
      inputs[i].pin = safe_pins[i];
    }
    Serial.println(F("[PREF] Board is SIM7670G - using safe GPIO pins (21,38,39,40)"));
  } else {
    // Generic board can use standard pins
    int generic_pins[MAX_INPUTS] = {1, 2, 3, 47};
    for (int i = 0; i < MAX_INPUTS; i++) {
      inputs[i].pin = generic_pins[i];
    }
    Serial.println(F("[PREF] Board is Generic - using standard GPIO pins (1,2,3,47)"));
  }
  Serial.flush();

  Serial.println(F("[PREF] Loading input configurations..."));
  Serial.flush();
  // Load input configurations
  for (int i = 0; i < MAX_INPUTS; i++) {
    Serial.printf("[PREF] Loading input %d...\n", i);
    Serial.flush();
    String prefix = "in" + String(i) + "_";
    inputs[i].enabled = preferences.getBool((prefix + "en").c_str(), inputs[i].enabled);
    inputs[i].pin = preferences.getInt((prefix + "pin").c_str(), inputs[i].pin);
    inputs[i].momentary_mode = preferences.getBool((prefix + "mom").c_str(), inputs[i].momentary_mode);
    inputs[i].name = preferences.getString((prefix + "name").c_str(), inputs[i].name);
    inputs[i].message_on = preferences.getString((prefix + "on").c_str(), inputs[i].message_on);
    inputs[i].message_off = preferences.getString((prefix + "off").c_str(), inputs[i].message_off);
    // NEW: Load camera and GPS settings per input
    inputs[i].capture_photo = preferences.getBool((prefix + "cam").c_str(), inputs[i].capture_photo);
    inputs[i].include_gps = preferences.getBool((prefix + "gps").c_str(), inputs[i].include_gps);
  }

  Serial.println(F("[PREF] Closing preferences..."));
  Serial.flush();
  preferences.end();
  Serial.println(F("[PREF] Preferences loaded successfully"));
  Serial.flush();
}

void savePreferences() {
  preferences.begin("notifier", false);

  preferences.putString(PrefKeys::WIFI_SSID, wifi_ssid);
  preferences.putString(PrefKeys::WIFI_PASS, wifi_password);
  preferences.putString(PrefKeys::WEB_USER, web_username);
  preferences.putString(PrefKeys::WEB_PASS, web_password);

  preferences.putString(PrefKeys::PB_TOKEN, pushbullet_token);
  preferences.putBool(PrefKeys::PB_ENABLED, pushbullet_enabled);

  preferences.putString(PrefKeys::SMTP_HOST, smtp_host);
  preferences.putInt(PrefKeys::SMTP_PORT, smtp_port);
  preferences.putString(PrefKeys::SMTP_EMAIL, smtp_email);
  preferences.putString(PrefKeys::SMTP_PASS, smtp_password);
  preferences.putString(PrefKeys::RCPT_EMAIL, recipient_email);
  preferences.putBool(PrefKeys::EMAIL_ENABLED, email_enabled);

  preferences.putString(PrefKeys::TG_TOKEN, telegram_token);
  preferences.putString(PrefKeys::TG_CHAT, telegram_chat_id);
  preferences.putBool(PrefKeys::TG_ENABLED, telegram_enabled);

  preferences.putString(PrefKeys::NOTIF_TITLE, notification_title);
  preferences.putLong(PrefKeys::GMT_OFFSET, gmt_offset);
  preferences.putInt(PrefKeys::DAY_OFFSET, daylight_offset);

  // NEW: Save board configuration
  preferences.putInt(PrefKeys::BOARD_TYPE, board_type);
  preferences.putBool(PrefKeys::CAMERA_EN, camera_enabled);
  preferences.putBool(PrefKeys::GPS_EN, gps_enabled);

  // NEW: v3.1 Save cellular configuration
  preferences.putBool(PrefKeys::CELL_EN, cellular_enabled);
  preferences.putString(PrefKeys::APN, apn);
  preferences.putInt(PrefKeys::PB_CONN_MODE, pushbullet_conn_mode);
  preferences.putInt(PrefKeys::EMAIL_CONN_MODE, email_conn_mode);
  preferences.putInt(PrefKeys::TG_CONN_MODE, telegram_conn_mode);
  preferences.putInt(PrefKeys::SMS_CONN_MODE, sms_conn_mode);
  preferences.putBool(PrefKeys::SMS_EN, sms_enabled);
  preferences.putString(PrefKeys::SMS_PHONE, sms_phone_number);

  // Save BMP180 sensor configuration
  preferences.putBool(PrefKeys::BMP180_EN, bmp180_enabled);
  preferences.putInt(PrefKeys::BMP180_SDA, bmp180_sda_pin);
  preferences.putInt(PrefKeys::BMP180_SCL, bmp180_scl_pin);
  preferences.putBool(PrefKeys::BMP180_IN_NOTIF, include_bmp180_in_notifications);

  // Save input configurations
  for (int i = 0; i < MAX_INPUTS; i++) {
    String prefix = "in" + String(i) + "_";
    preferences.putBool((prefix + "en").c_str(), inputs[i].enabled);
    preferences.putInt((prefix + "pin").c_str(), inputs[i].pin);
    preferences.putBool((prefix + "mom").c_str(), inputs[i].momentary_mode);
    preferences.putString((prefix + "name").c_str(), inputs[i].name);
    preferences.putString((prefix + "on").c_str(), inputs[i].message_on);
    preferences.putString((prefix + "off").c_str(), inputs[i].message_off);
    // NEW: Save camera and GPS settings per input
    preferences.putBool((prefix + "cam").c_str(), inputs[i].capture_photo);
    preferences.putBool((prefix + "gps").c_str(), inputs[i].include_gps);
  }

  preferences.end();
  Serial.println(F("Preferences saved"));
}

String htmlEncode(String str) {
  str.replace("&", "&amp;");
  str.replace("<", "&lt;");
  str.replace(">", "&gt;");
  str.replace("\"", "&quot;");
  str.replace("'", "&#39;");
  return str;
}

void setupWebServer() {
  server.on("/", []() {
    // No authentication required in AP mode
    if (!apMode && !server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleRoot();
  });

  server.on("/saveWiFi", HTTP_POST, []() {
    handleSaveWiFi();
  });

  server.on("/scanWiFi", []() {
    handleScanWiFi();
  });

  server.on("/save", HTTP_POST, []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleSave();
  });

  server.on("/status", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleStatus();
  });

  server.on("/restart", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleRestart();
  });

  server.on("/test/pushbullet", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestPushbullet();
  });

  server.on("/test/email", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestEmail();
  });

  server.on("/test/telegram", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestTelegram();
  });

  // NEW v3.0: Camera test endpoint
  server.on("/test/camera", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestCamera();
  });

  server.on("/test/input", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestInput();
  });

  server.on("/test/sms", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestSMS();
  });

  server.on("/test/bmp180", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestBMP180();
  });

  server.on("/search/bmp180", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleSearchBMP180();
  });

  server.on("/test/gpio", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestGPIO();
  });

  server.on("/test/cellular", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleTestCellular();
  });

  server.on("/logs", []() {
    if (!apMode && !server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleLogs();
  });

  server.on("/resetWiFi", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleResetWiFi();
  });

  server.on("/api/config", []() {
    if (!apMode && !server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleGetConfig();
  });

  server.on("/gallery", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleGallery();
  });

  server.on("/api/photos", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handlePhotoList();
  });

  server.on("/photo", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handlePhotoDownload();
  });

  server.on("/cellular", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleCellularDiagnostics();
  });

  server.on("/api/cellular", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleCellularAPI();
  });

  server.on("/cellular/refresh", []() {
    if (!server.authenticate(web_username.c_str(), web_password.c_str())) {
      return server.requestAuthentication();
    }
    handleCellularRefresh();
  });
}

void handleRoot() {
  // If in AP mode, show WiFi setup page
  if (apMode) {
    handleWiFiSetup();
    return;
  }

  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP32-Notifier v");
  html += VERSION;
  html += F("</title><style>"
  ":root{--bg:#f0f0f0;--card:#fff;--text:#333;--text2:#555;--text3:#666;--border:#ddd;--section:#f9f9f9;--status:#e7f3fe;--status-border:#2196F3}"
  "body.dark{--bg:#1a1a1a;--card:#2d2d2d;--text:#e0e0e0;--text2:#b0b0b0;--text3:#909090;--border:#404040;--section:#252525;--status:#1e3a5f;--status-border:#4fc3f7}"
  "body{font-family:Arial;margin:20px;background:var(--bg);color:var(--text);transition:all 0.3s}"
  ".c{max-width:800px;margin:auto;background:var(--card);padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
  "h1{color:var(--text);display:flex;justify-content:space-between;align-items:center}"
  ".theme-toggle{background:none;border:2px solid var(--border);padding:6px 12px;cursor:pointer;border-radius:4px;font-size:18px;transition:all 0.3s}"
  ".theme-toggle:hover{border-color:var(--text3);transform:scale(1.1)}"
  ".s{margin:15px 0;padding:12px;background:var(--section);border-radius:5px;border:1px solid var(--border)}.s h2{margin-top:0;color:var(--text2);font-size:16px}"
  "label{display:block;margin:8px 0 4px;font-weight:bold;color:var(--text3);font-size:14px}"
  "input,textarea,select{width:100%;padding:6px;box-sizing:border-box;border:1px solid var(--border);border-radius:4px;font-size:13px;background:var(--card);color:var(--text)}"
  "input[type=checkbox]{width:auto;margin-right:6px}textarea{min-height:50px;font-family:monospace}"
  "button{background:#4CAF50;color:#fff;padding:8px 16px;border:none;border-radius:4px;cursor:pointer;margin:4px 4px 4px 0;font-size:13px;transition:all 0.2s}"
  "button:hover{opacity:0.9;transform:translateY(-1px)}"
  ".b1{background:#008CBA}.b2{background:#f44336}.b3{background:#FF9800}.b4{background:#9C27B0}"
  ".st{padding:8px;background:var(--status);border-left:4px solid var(--status-border);margin:10px 0;font-size:13px;border-radius:4px}"
  ".inp{border:1px solid var(--border);padding:10px;margin:10px 0;background:var(--section);border-radius:4px}"
  ".badge{display:inline-block;padding:3px 8px;border-radius:12px;font-size:11px;font-weight:bold;margin-left:8px}"
  ".badge.success{background:#4CAF50;color:#fff}.badge.error{background:#f44336;color:#fff}.badge.warning{background:#FF9800;color:#fff}"
  "</style></head><body><div class='c'><h1><span>ESP32-Notifier v");
  html += VERSION;
  html += F("</span><button type='button' class='theme-toggle' onclick='toggleTheme()' title='Toggle Dark Mode'>🌓</button></h1>"
  "<div class='st' id='sb'><b>Status:</b> <span id='st'>Loading...</span></div>");

  html += F("<form method='POST' action='/save'>");

  // WiFi
  html += F("<div class='s'><h2>WiFi</h2><label>SSID:</label><input name='wifi_ssid' value='");
  html += htmlEncode(wifi_ssid);
  html += F("' required><label>Password:</label><input type='password' name='wifi_password' value='");
  html += htmlEncode(wifi_password);
  html += F("'><button type='button' class='b2' style='margin-top:10px' onclick=\"if(confirm('Reset WiFi credentials and restart in AP mode?'))location.href='/resetWiFi'\">Reset WiFi</button></div>");

  // Web Auth
  html += F("<div class='s'><h2>Web Authentication</h2><label>Username:</label><input name='web_user' value='");
  html += htmlEncode(web_username);
  html += F("'><label>Password:</label><input type='password' name='web_pass' value='");
  html += htmlEncode(web_password);
  html += F("'></div>");

  // NEW v3.0: Board Configuration
  html += F("<div class='s'><h2>Board Configuration</h2><label>Board Type:</label><select name='board_type'>");
  html += F("<option value='0'");
  if (board_type == BOARD_GENERIC) html += F(" selected");
  html += F(">Generic ESP32-S3</option>");
  html += F("<option value='1'");
  if (board_type == BOARD_SIM7670G) html += F(" selected");
  html += F(">ESP32-S3-SIM7670G-4G (Waveshare)</option>");
  html += F("<option value='2'");
  if (board_type == BOARD_FREENOVE_S3) html += F(" selected");
  html += F(">Freenove ESP32-S3 CAM</option></select>");

  // Camera and GPS options (only shown for SIM7670G board)
  html += F("<div style='margin-top:10px'><label><input type='checkbox' name='camera_en' value='1'");
  if (camera_enabled) html += F(" checked");
  html += F(">Enable Camera (OV2640)</label>");
  if (camera_enabled && camera_initialized) {
    html += F("<button type='button' class='b3' onclick='testService(\"camera\")'>Test Camera</button>");
  }
  html += F("<label><input type='checkbox' name='gps_en' value='1'");
  if (gps_enabled) html += F(" checked");
  html += F(">Enable GPS/GNSS</label>");

  // Status indicators
  if (board_type == BOARD_SIM7670G) {
    html += F("<p style='font-size:12px;margin-top:8px;color:#666'>");
    if (camera_initialized) html += F("📷 Camera: Ready<br>");
    if (sd_card_available) {
      #ifdef HAS_CAMERA_LIB
      uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
      html += F("💾 SD Card: ");
      html += String((uint32_t)cardSize);
      html += F(" MB<br>");
      #endif
    }
    #ifdef HAS_GPS_LIB
    if (gps_enabled && gps != nullptr) {
      html += F("🛰️ GPS: ");
      html += gps->satellites.isValid() ? "Fix (" + String(gps->satellites.value()) + " sats)" : "Searching";
    }
    #endif
    html += F("</p>");
  }
  html += F("</div></div>");

  // NEW v3.1: Cellular/4G Configuration
  if (board_type == BOARD_SIM7670G) {
    html += F("<div class='s'><h2>Cellular / 4G Configuration</h2>");
    html += F("<label><input type='checkbox' name='cell_en' value='1'");
    if (cellular_enabled) html += F(" checked");
    html += F(">Enable Cellular Modem</label>");
    html += F("<label>APN (Access Point Name):</label><input name='apn' value='");
    html += htmlEncode(apn);
    html += F("' placeholder='e.g., internet, wholesale, etc.'>");

    // Status indicators
    html += F("<p style='font-size:12px;margin-top:8px;color:#666'>");
    if (cellular_connected) {
      html += F("📶 Cellular: Connected to ");
      html += cellular_operator;
      html += F(" (Signal: ");
      html += String(cellular_signal_strength);
      html += F(")<br>");
    } else if (cellular_enabled) {
      html += F("📶 Cellular: Disconnected<br>");
    }
    html += F("</p>");
    html += F("<button type='button' class='b3' onclick='testService(\"cellular\")'>Test Cellular Modem</button>");
    html += F("</div>");
  }

  // BMP180 Sensor Configuration
  html += F("<div class='s'><h2>BMP180 Sensor (Temperature/Pressure)</h2>");
  html += F("<label><input type='checkbox' name='bmp180_en' value='1'");
  if (bmp180_enabled) html += F(" checked");
  html += F(">Enable BMP180 Sensor</label>");
  html += F("<label>SDA Pin (I2C Data):</label><input type='number' name='bmp180_sda' value='");
  html += String(bmp180_sda_pin);
  html += F("' min='0' max='48'>");
  html += F("<label>SCL Pin (I2C Clock):</label><input type='number' name='bmp180_scl' value='");
  html += String(bmp180_scl_pin);
  html += F("' min='0' max='48'>");
  html += F("<label><input type='checkbox' name='bmp180_notif' value='1'");
  if (include_bmp180_in_notifications) html += F(" checked");
  html += F(">Include sensor data in notifications</label>");

  // Status indicators
  html += F("<p style='font-size:12px;margin-top:8px;color:#666'>");
  if (bmp180_initialized) {
    html += F("✓ Sensor: Initialized<br>");
    html += F("🌡️ Temperature: ");
    html += String(last_temperature, 1);
    html += F("°C / ");
    html += String(last_temperature * 9.0 / 5.0 + 32.0, 1);
    html += F("°F<br>");
    html += F("🔽 Pressure: ");
    html += String(last_pressure, 1);
    html += F(" hPa / ");
    html += String(last_pressure * 0.02953, 2);
    html += F(" inHg<br>");
    html += F("⛰️ Altitude: ");
    html += String(last_altitude, 1);
    html += F(" m / ");
    html += String(last_altitude * 3.28084, 1);
    html += F(" ft");
  } else if (bmp180_enabled) {
    html += F("⚠️ Sensor not detected - check wiring");
  } else {
    html += F("Sensor disabled");
  }
  html += F("</p>");
  html += F("<button type='button' class='b3' onclick='testService(\"bmp180\")'>Test / Read Sensor</button> ");
  html += F("<button type='button' class='b3' onclick='searchBMP180()'>Search for BMP180</button> ");
  html += F("<button type='button' class='b2' onclick='testGPIO()'>⚡ Test GPIO Pins</button>");
  html += F("<p style='font-size:11px;color:#999;margin-top:5px'>");
  html += F("Use: SDA=21 (Pin 22), SCL=33 (Pin 16). NOTE: Input 1 disabled (GPIO 21 used by BMP180). Only 2 GPIOs available on this board.");
  html += F("</p>");
  html += F("</div>");

  // Pushbullet
  html += F("<div class='s'><h2>Pushbullet</h2><label><input type='checkbox' name='pb_enabled' value='1'");
  if (pushbullet_enabled) html += F(" checked");
  html += F(">Enable</label><label>Token:</label><input name='pb_token' value='");
  html += htmlEncode(pushbullet_token);
  html += F("'>");

  // NEW v3.1: Connection mode selector
  if (board_type == BOARD_SIM7670G && cellular_enabled) {
    html += F("<label>Connection Mode:</label><select name='pb_conn_mode'>");
    html += F("<option value='0'");
    if (pushbullet_conn_mode == CONN_WIFI_ONLY) html += F(" selected");
    html += F(">WiFi Only</option>");
    html += F("<option value='1'");
    if (pushbullet_conn_mode == CONN_CELL_ONLY) html += F(" selected");
    html += F(">Cellular Only</option>");
    html += F("<option value='2'");
    if (pushbullet_conn_mode == CONN_WIFI_CELL_BACKUP) html += F(" selected");
    html += F(">WiFi with Cellular Backup</option></select>");
  }

  html += F("<button type='button' class='b3' onclick='testService(\"pushbullet\")'>Test</button></div>");

  // Email
  html += F("<div class='s'><h2>Email</h2><label><input type='checkbox' name='email_enabled' value='1'");
  if (email_enabled) html += F(" checked");
  html += F(">Enable</label><label>Server:</label><input name='smtp_host' value='");
  html += htmlEncode(smtp_host);
  html += F("'><label>Port:</label><input type='number' name='smtp_port' value='");
  html += String(smtp_port);
  html += F("'><label>Email:</label><input name='smtp_email' value='");
  html += htmlEncode(smtp_email);
  html += F("'><label>Password:</label><input type='password' name='smtp_password' value='");
  html += htmlEncode(smtp_password);
  html += F("'><label>Recipient:</label><input name='recipient_email' value='");
  html += htmlEncode(recipient_email);
  html += F("'>");

  // NEW v3.1: Connection mode selector
  if (board_type == BOARD_SIM7670G && cellular_enabled) {
    html += F("<label>Connection Mode:</label><select name='email_conn_mode'>");
    html += F("<option value='0'");
    if (email_conn_mode == CONN_WIFI_ONLY) html += F(" selected");
    html += F(">WiFi Only</option>");
    html += F("<option value='1'");
    if (email_conn_mode == CONN_CELL_ONLY) html += F(" selected");
    html += F(">Cellular Only</option>");
    html += F("<option value='2'");
    if (email_conn_mode == CONN_WIFI_CELL_BACKUP) html += F(" selected");
    html += F(">WiFi with Cellular Backup</option></select>");
  }

  html += F("<button type='button' class='b3' onclick='testService(\"email\")'>Test</button></div>");

  // Telegram
  html += F("<div class='s'><h2>Telegram</h2><label><input type='checkbox' name='tg_enabled' value='1'");
  if (telegram_enabled) html += F(" checked");
  html += F(">Enable</label><label>Bot Token:</label><input name='tg_token' value='");
  html += htmlEncode(telegram_token);
  html += F("'><label>Chat ID:</label><input name='tg_chat' value='");
  html += htmlEncode(telegram_chat_id);
  html += F("'>");

  // NEW v3.1: Connection mode selector
  if (board_type == BOARD_SIM7670G && cellular_enabled) {
    html += F("<label>Connection Mode:</label><select name='tg_conn_mode'>");
    html += F("<option value='0'");
    if (telegram_conn_mode == CONN_WIFI_ONLY) html += F(" selected");
    html += F(">WiFi Only</option>");
    html += F("<option value='1'");
    if (telegram_conn_mode == CONN_CELL_ONLY) html += F(" selected");
    html += F(">Cellular Only</option>");
    html += F("<option value='2'");
    if (telegram_conn_mode == CONN_WIFI_CELL_BACKUP) html += F(" selected");
    html += F(">WiFi with Cellular Backup</option></select>");
  }

  html += F("<button type='button' class='b3' onclick='testService(\"telegram\")'>Test</button></div>");

  // NEW v3.1: SMS Configuration
  if (board_type == BOARD_SIM7670G && cellular_enabled) {
    html += F("<div class='s'><h2>SMS Notifications</h2>");
    html += F("<label><input type='checkbox' name='sms_en' value='1'");
    if (sms_enabled) html += F(" checked");
    html += F(">Enable SMS</label>");
    html += F("<label>Phone Number:</label><input name='sms_phone' value='");
    html += htmlEncode(sms_phone_number);
    html += F("' placeholder='e.g., +15551234567'>");
    html += F("<label>Connection Mode:</label><select name='sms_conn_mode'>");
    html += F("<option value='0'");
    if (sms_conn_mode == CONN_WIFI_ONLY) html += F(" selected");
    html += F(">WiFi Only</option>");
    html += F("<option value='1'");
    if (sms_conn_mode == CONN_CELL_ONLY) html += F(" selected");
    html += F(">Cellular Only</option>");
    html += F("<option value='2'");
    if (sms_conn_mode == CONN_WIFI_CELL_BACKUP) html += F(" selected");
    html += F(">WiFi with Cellular Backup</option></select>");
    html += F("<button type='button' class='b3' onclick='testService(\"sms\")'>Test SMS</button>");
    html += F("<p style='font-size:12px;margin-top:8px;color:#666'>Note: SMS is sent via cellular modem. Messages are limited to 160 characters.</p>");
    html += F("</div>");
  }

  // Inputs
  html += F("<div class='s'><h2>Inputs Configuration</h2>");
  for (int i = 0; i < MAX_INPUTS; i++) {
    html += F("<div class='inp'><h3>Input ");
    html += String(i + 1);
    html += F("</h3><label><input type='checkbox' name='in");
    html += String(i);
    html += F("_en' value='1'");
    if (inputs[i].enabled) html += F(" checked");
    html += F(">Enabled</label><label>Name:</label><input name='in");
    html += String(i);
    html += F("_name' value='");
    html += htmlEncode(inputs[i].name);
    html += F("'><label>GPIO Pin:</label><input type='number' name='in");
    html += String(i);
    html += F("_pin' value='");
    html += String(inputs[i].pin);
    html += F("' min='0' max='48'><label>Mode:</label><select name='in");
    html += String(i);
    html += F("_mode'><option value='toggle'");
    if (!inputs[i].momentary_mode) html += F(" selected");
    html += F(">Toggle</option><option value='momentary'");
    if (inputs[i].momentary_mode) html += F(" selected");
    html += F(">Momentary</option></select><label>ON Message:</label><textarea name='in");
    html += String(i);
    html += F("_on'>");
    html += htmlEncode(inputs[i].message_on);
    html += F("</textarea><label>OFF Message:</label><textarea name='in");
    html += String(i);
    html += F("_off'>");
    html += htmlEncode(inputs[i].message_off);
    html += F("</textarea>");

    // NEW v3.0: Camera and GPS options per input
    if (board_type == BOARD_SIM7670G) {
      html += F("<div style='margin-top:10px;padding-top:10px;border-top:1px solid #ddd'>");
      html += F("<label><input type='checkbox' name='in");
      html += String(i);
      html += F("_cam' value='1'");
      if (inputs[i].capture_photo) html += F(" checked");
      html += F(">📷 Capture Photo on Trigger</label>");
      html += F("<label><input type='checkbox' name='in");
      html += String(i);
      html += F("_gps' value='1'");
      if (inputs[i].include_gps) html += F(" checked");
      html += F(">🛰️ Include GPS Location</label>");
      html += F("</div>");
    }

    // Manual testing section
    if (inputs[i].enabled) {
      html += F("<div style='margin-top:10px;padding-top:10px;border-top:1px solid #ddd'>");
      html += F("<label>Current State: <span id='inp");
      html += String(i);
      html += F("_state' class='badge'>...</span></label>");
      html += F("<button type='button' class='b3' onclick='testInput(");
      html += String(i);
      html += F(")'>🧪 Test Trigger</button>");
      html += F("</div>");
    }

    html += F("</div>");
  }
  html += F("</div>");

  // General settings
  html += F("<div class='s'><h2>General</h2><label>Title:</label><input name='notif_title' value='");
  html += htmlEncode(notification_title);
  html += F("'><label>Timezone:</label><select name='gmt_offset'>"
  "<option value='-43200'>UTC-12 (Baker Island)</option>"
  "<option value='-39600'>UTC-11 (Samoa)</option>"
  "<option value='-36000'>UTC-10 (Hawaii)</option>"
  "<option value='-32400'>UTC-9 (Alaska)</option>"
  "<option value='-28800'>UTC-8 (Pacific Time)</option>"
  "<option value='-25200'>UTC-7 (Mountain Time)</option>"
  "<option value='-21600'>UTC-6 (Central Time)</option>"
  "<option value='-18000'>UTC-5 (Eastern Time)</option>"
  "<option value='-14400'>UTC-4 (Atlantic Time)</option>"
  "<option value='-12600'>UTC-3.5 (Newfoundland)</option>"
  "<option value='-10800'>UTC-3 (Brazil, Argentina)</option>"
  "<option value='-7200'>UTC-2 (Mid-Atlantic)</option>"
  "<option value='-3600'>UTC-1 (Azores)</option>"
  "<option value='0'>UTC+0 (London, Lisbon)</option>"
  "<option value='3600'>UTC+1 (Paris, Berlin)</option>"
  "<option value='7200'>UTC+2 (Athens, Cairo)</option>"
  "<option value='10800'>UTC+3 (Moscow, Istanbul)</option>"
  "<option value='12600'>UTC+3.5 (Tehran)</option>"
  "<option value='14400'>UTC+4 (Dubai, Baku)</option>"
  "<option value='16200'>UTC+4.5 (Kabul)</option>"
  "<option value='18000'>UTC+5 (Pakistan, Maldives)</option>"
  "<option value='19800'>UTC+5.5 (India, Sri Lanka)</option>"
  "<option value='20700'>UTC+5.75 (Nepal)</option>"
  "<option value='21600'>UTC+6 (Bangladesh, Bhutan)</option>"
  "<option value='23400'>UTC+6.5 (Myanmar)</option>"
  "<option value='25200'>UTC+7 (Bangkok, Jakarta)</option>"
  "<option value='28800'>UTC+8 (China, Singapore)</option>"
  "<option value='31500'>UTC+8.75 (Eucla, Australia)</option>"
  "<option value='32400'>UTC+9 (Tokyo, Seoul)</option>"
  "<option value='34200'>UTC+9.5 (Adelaide)</option>"
  "<option value='36000'>UTC+10 (Sydney, Melbourne)</option>"
  "<option value='37800'>UTC+10.5 (Lord Howe Island)</option>"
  "<option value='39600'>UTC+11 (Solomon Islands)</option>"
  "<option value='43200'>UTC+12 (New Zealand, Fiji)</option>"
  "<option value='45900'>UTC+12.75 (Chatham Islands)</option>"
  "<option value='46800'>UTC+13 (Tonga)</option>"
  "<option value='50400'>UTC+14 (Line Islands)</option>"
  "</select><label>DST Offset (sec):</label><input type='number' name='day_offset' value='");
  html += String(daylight_offset);
  html += F("'></div>");

  html += F("<button type='submit'>Save Configuration</button><button type='button' class='b1' onclick='location.reload()'>Refresh</button>");
  html += F("<button type='button' class='b3' onclick=\"location.href='/logs'\">View Logs</button>");

  // Add photo gallery button if camera and SD card are available
  if (camera_enabled && camera_initialized && sd_card_available) {
    html += F("<button type='button' class='b4' onclick=\"location.href='/gallery'\">📷 Photo Gallery</button>");
  }

  html += F("<button type='button' class='b3' onclick='downloadConfig()'>💾 Backup Config</button>");

  // Add cellular diagnostics button if board supports it
  if (board_type == BOARD_SIM7670G) {
    html += F("<button type='button' class='b4' onclick=\"location.href='/cellular'\">📶 Cellular Diagnostics</button>");
  }

  html += F("<button type='button' class='b2' onclick=\"if(confirm('Restart?'))location.href='/restart'\">Restart</button></form></div>");

  html += F("<script>"
  "function toggleTheme(){document.body.classList.toggle('dark');localStorage.setItem('theme',document.body.classList.contains('dark')?'dark':'light')}"
  "if(localStorage.getItem('theme')==='dark')document.body.classList.add('dark');"
  "function testService(s){fetch('/test/'+s).then(r=>r.text()).then(d=>alert(s+': '+d)).catch(e=>alert('Error: '+e))}"
  "function testInput(i){if(confirm('Trigger Input '+(i+1)+'?')){fetch('/test/input?id='+i).then(r=>r.text()).then(d=>alert(d)).catch(e=>alert('Error: '+e))}}"
  "function searchBMP180(){"
  "fetch('/search/bmp180').then(r=>r.text()).then(d=>{"
  "const modal=document.createElement('div');"
  "modal.style.cssText='position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.7);display:flex;align-items:center;justify-content:center;z-index:9999';"
  "const box=document.createElement('div');"
  "box.style.cssText='background:var(--card);padding:20px;border-radius:8px;max-width:600px;width:90%';"
  "box.innerHTML='<h3>I2C Bus Scan Results</h3><textarea readonly style=\"width:100%;height:300px;font-family:monospace;font-size:12px;margin:10px 0;padding:10px\">'+d+'</textarea>"
  "<button onclick=\"navigator.clipboard.writeText(this.previousElementSibling.value).then(()=>alert(\\'Copied to clipboard!\\'))\">Copy to Clipboard</button> "
  "<button onclick=\"this.closest(\\'div\\').parentElement.remove()\">Close</button>';"
  "modal.appendChild(box);document.body.appendChild(modal);modal.onclick=e=>{if(e.target===modal)modal.remove()}"
  "}).catch(e=>alert('Error: '+e))}"
  "function testGPIO(){"
  "fetch('/test/gpio').then(r=>r.text()).then(d=>{"
  "const modal=document.createElement('div');"
  "modal.style.cssText='position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.7);display:flex;align-items:center;justify-content:center;z-index:9999';"
  "const box=document.createElement('div');"
  "box.style.cssText='background:var(--card);padding:20px;border-radius:8px;max-width:600px;width:90%';"
  "box.innerHTML='<h3>GPIO Pin Test Results</h3><textarea readonly style=\"width:100%;height:300px;font-family:monospace;font-size:12px;margin:10px 0;padding:10px\">'+d+'</textarea>"
  "<button onclick=\"navigator.clipboard.writeText(this.previousElementSibling.value).then(()=>alert(\\'Copied!\\'))\">Copy to Clipboard</button> "
  "<button onclick=\"this.closest(\\'div\\').parentElement.remove()\">Close</button>';"
  "modal.appendChild(box);document.body.appendChild(modal);modal.onclick=e=>{if(e.target===modal)modal.remove()}"
  "}).catch(e=>alert('Error: '+e))}"
  "function downloadConfig(){fetch('/api/config').then(r=>r.json()).then(d=>{"
  "const blob=new Blob([JSON.stringify(d,null,2)],{type:'application/json'});"
  "const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;"
  "a.download='esp32-notifier-config.json';a.click();URL.revokeObjectURL(url);"
  "}).catch(e=>alert('Error downloading config: '+e))}"
  "function updateStatus(){fetch('/status').then(r=>r.json()).then(d=>{"
  "let s='WiFi:'+(d.wifi?'✓':'✗')+' |IP:'+d.ip+' |Up:'+Math.floor(d.uptime/1000)+'s';"
  "if(d.heap)s+=' |Heap:'+Math.round(d.heap/1024)+'KB';"
  "d.inputs.forEach((inp,i)=>{"
  "s+=' |In'+(i+1)+':'+(inp?'H':'L');"
  "const el=document.getElementById('inp'+i+'_state');"
  "if(el){el.textContent=inp?'HIGH':'LOW';el.className='badge '+(inp?'warning':'success')}"
  "});"
  "document.getElementById('st').innerHTML=s}).catch(e=>console.error('Status update failed:',e))}"
  "updateStatus();setInterval(updateStatus,5000);"
  "document.querySelector('[name=gmt_offset]').value='");
  html += String(gmt_offset);
  html += F("';</script></body></html>");

  server.send(200, "text/html", html);
}

void handleWiFiSetup() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>ESP32-Notifier Setup</title><style>"
  "body{font-family:Arial;margin:20px;background:#f0f0f0}.c{max-width:600px;margin:auto;background:#fff;padding:20px;border-radius:8px}"
  "h1{color:#333;text-align:center}.s{margin:15px 0;padding:12px;background:#f9f9f9;border-radius:5px}"
  "label{display:block;margin:8px 0 4px;font-weight:bold;color:#666}input,select{width:100%;padding:8px;box-sizing:border-box;border:1px solid #ddd;border-radius:4px}"
  "button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px;margin-top:10px}"
  "button:hover{background:#45a049}.info{background:#e7f3fe;border-left:4px solid #2196F3;padding:10px;margin:15px 0}"
  ".net{padding:10px;margin:5px 0;background:#fff;border:1px solid #ddd;border-radius:4px;cursor:pointer;display:flex;justify-content:space-between;align-items:center}"
  ".net:hover{background:#f0f0f0}.scan-btn{background:#2196F3;margin-bottom:10px}.scan-btn:hover{background:#0b7dda}"
  ".signal{font-size:12px;color:#666}#networks{max-height:300px;overflow-y:auto}"
  "</style></head><body><div class='c'><h1>ESP32-Notifier Setup</h1>"
  "<div class='info'>Welcome! Connect to your WiFi network to get started.</div>"
  "<div class='s'><h2>Available Networks</h2>"
  "<button type='button' class='scan-btn' onclick='scanNetworks()'>Scan for Networks</button>"
  "<div id='networks'><p style='color:#666;text-align:center'>Click 'Scan for Networks' to see available WiFi networks</p></div></div>"
  "<form method='POST' action='/saveWiFi'><div class='s'><h2>WiFi Configuration</h2>"
  "<label>Network Name (SSID):</label><input id='ssid' name='ssid' required placeholder='Your WiFi Network'>"
  "<label>Password:</label><input type='password' name='password' required placeholder='WiFi Password'>"
  "</div><button type='submit'>Connect to WiFi</button></form>"
  "<div class='s' style='margin-top:20px'><small>After connecting, the device will restart and join your WiFi network. "
  "You can then access the full configuration page using the IP address shown in the Serial Monitor.</small></div>"
  "</div><script>"
  "function scanNetworks(){"
  "document.getElementById('networks').innerHTML='<p style=\"text-align:center\">Scanning...</p>';"
  "fetch('/scanWiFi').then(r=>r.json()).then(d=>{"
  "let h='';if(d.networks.length==0)h='<p style=\"color:#666;text-align:center\">No networks found</p>';else{"
  "d.networks.forEach(n=>{"
  "let bars='';for(let i=0;i<4;i++)bars+=(n.rssi>(-90+i*10)?'▂':' ');"
  "h+='<div class=\"net\" onclick=\"selectNetwork(\\''+n.ssid+'\\')\">';"
  "h+='<span>'+n.ssid+(n.enc?' 🔒':'')+'</span>';"
  "h+='<span class=\"signal\">'+bars+'</span></div>';})}"
  "document.getElementById('networks').innerHTML=h;"
  "}).catch(e=>{document.getElementById('networks').innerHTML='<p style=\"color:red\">Scan failed</p>';});}"
  "function selectNetwork(ssid){document.getElementById('ssid').value=ssid;document.getElementById('ssid').focus();}"
  "</script></body></html>");

  server.send(200, "text/html", html);
}

void handleSaveWiFi() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    wifi_ssid = server.arg("ssid");
    wifi_password = server.arg("password");

    savePreferences();

    String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Connecting...</title><style>"
    "body{font-family:Arial;margin:20px;background:#f0f0f0;text-align:center}.c{max-width:600px;margin:auto;background:#fff;padding:40px;border-radius:8px}"
    "h1{color:#4CAF50}</style></head><body><div class='c'><h1>Connecting to WiFi...</h1>"
    "<p>Device is restarting and connecting to your network.</p>"
    "<p>Please check your Serial Monitor for the new IP address.</p>"
    "<p>This window will close automatically.</p></div>"
    "<script>setTimeout(function(){window.close()},5000);</script></body></html>");

    server.send(200, "text/html", html);

    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing WiFi credentials");
  }
}

void handleScanWiFi() {
  Serial.println(F("Scanning WiFi networks..."));
  int n = WiFi.scanNetworks();

  String json = "{\"networks\":[";

  if (n > 0) {
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      json += "{";
      json += "\"ssid\":\"";

      // Properly escape SSID for JSON
      String ssid = WiFi.SSID(i);
      for (unsigned int j = 0; j < ssid.length(); j++) {
        char c = ssid.charAt(j);
        if (c == '"' || c == '\\') {
          json += '\\';
        }
        if (c >= 32 && c <= 126) {  // Printable ASCII only
          json += c;
        }
      }

      json += "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      json += "\"enc\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
      json += "}";
    }
  }

  json += "]}";

  WiFi.scanDelete();
  Serial.println(F("WiFi scan complete"));

  server.send(200, "application/json", json);
}

void handleSave() {
  if (server.hasArg("wifi_ssid")) wifi_ssid = server.arg("wifi_ssid");
  if (server.hasArg("wifi_password")) wifi_password = server.arg("wifi_password");
  if (server.hasArg("web_user")) web_username = server.arg("web_user");
  if (server.hasArg("web_pass")) web_password = server.arg("web_pass");

  if (server.hasArg("pb_token")) pushbullet_token = server.arg("pb_token");
  pushbullet_enabled = server.hasArg("pb_enabled");

  if (server.hasArg("smtp_host")) smtp_host = server.arg("smtp_host");
  if (server.hasArg("smtp_port")) smtp_port = server.arg("smtp_port").toInt();
  if (server.hasArg("smtp_email")) smtp_email = server.arg("smtp_email");
  if (server.hasArg("smtp_password")) smtp_password = server.arg("smtp_password");
  if (server.hasArg("recipient_email")) recipient_email = server.arg("recipient_email");
  email_enabled = server.hasArg("email_enabled");

  if (server.hasArg("tg_token")) telegram_token = server.arg("tg_token");
  if (server.hasArg("tg_chat")) telegram_chat_id = server.arg("tg_chat");
  telegram_enabled = server.hasArg("tg_enabled");

  if (server.hasArg("notif_title")) notification_title = server.arg("notif_title");

  // NEW v3.0: Board configuration
  if (server.hasArg("board_type")) {
    int newBoardType = server.arg("board_type").toInt();
    if (newBoardType != board_type) {
      board_type = newBoardType;
      // Board type changed - may need restart to re-initialize hardware
      addLog("INFO", "Board type changed - restart recommended");
    }
  }
  camera_enabled = server.hasArg("camera_en");
  gps_enabled = server.hasArg("gps_en");

  // NEW v3.1: Cellular configuration
  cellular_enabled = server.hasArg("cell_en");
  if (server.hasArg("apn")) apn = server.arg("apn");

  // Connection modes per service
  if (server.hasArg("pb_conn_mode")) pushbullet_conn_mode = server.arg("pb_conn_mode").toInt();
  if (server.hasArg("email_conn_mode")) email_conn_mode = server.arg("email_conn_mode").toInt();
  if (server.hasArg("tg_conn_mode")) telegram_conn_mode = server.arg("tg_conn_mode").toInt();
  if (server.hasArg("sms_conn_mode")) sms_conn_mode = server.arg("sms_conn_mode").toInt();

  // SMS configuration
  sms_enabled = server.hasArg("sms_en");
  if (server.hasArg("sms_phone")) sms_phone_number = server.arg("sms_phone");

  // BMP180 sensor configuration
  bool bmp180_was_enabled = bmp180_enabled;
  int old_sda = bmp180_sda_pin;
  int old_scl = bmp180_scl_pin;

  bmp180_enabled = server.hasArg("bmp180_en");
  if (server.hasArg("bmp180_sda")) bmp180_sda_pin = server.arg("bmp180_sda").toInt();
  if (server.hasArg("bmp180_scl")) bmp180_scl_pin = server.arg("bmp180_scl").toInt();
  include_bmp180_in_notifications = server.hasArg("bmp180_notif");

  // Re-initialize sensor if settings changed (defer until after HTTP response)
  if (bmp180_enabled && (!bmp180_was_enabled || old_sda != bmp180_sda_pin || old_scl != bmp180_scl_pin)) {
    Serial.println(F("\n[SAVE] BMP180 configuration changed:"));
    if (!bmp180_was_enabled) {
      Serial.println(F("[SAVE]   - BMP180 enabled"));
    }
    if (old_sda != bmp180_sda_pin || old_scl != bmp180_scl_pin) {
      Serial.printf("[SAVE]   - Pin change: SDA GPIO%d->GPIO%d, SCL GPIO%d->GPIO%d\n",
                    old_sda, bmp180_sda_pin, old_scl, bmp180_scl_pin);
    }
    Serial.println(F("[SAVE] Will reinitialize BMP180 after saving settings..."));
    Serial.flush();
    addLog("INFO", "BMP180 settings changed - will reinitialize after save");
    bmp180_initialized = false;
    bmp180_needs_reinit = true;  // Defer until after HTTP response sent
  } else if (!bmp180_enabled && bmp180_was_enabled) {
    Serial.println(F("\n[SAVE] BMP180 sensor disabled"));
    Serial.flush();
    addLog("INFO", "BMP180 sensor disabled");
    bmp180_initialized = false;
    bmp180_needs_reinit = false;
  }

  // Handle time zone changes (defer configTime to prevent blocking)
  if (server.hasArg("gmt_offset")) {
    long newOffset = server.arg("gmt_offset").toInt();
    if (newOffset != gmt_offset) {
      gmt_offset = newOffset;
      pending_gmt_offset = gmt_offset;
      pending_daylight_offset = daylight_offset;
      needs_time_update = true;  // Defer configTime() call
    }
  }
  if (server.hasArg("day_offset")) {
    int newOffset = server.arg("day_offset").toInt();
    if (newOffset != daylight_offset) {
      daylight_offset = newOffset;
      pending_gmt_offset = gmt_offset;
      pending_daylight_offset = daylight_offset;
      needs_time_update = true;  // Defer configTime() call
    }
  }

  // Save input configurations
  for (int i = 0; i < MAX_INPUTS; i++) {
    String prefix = "in" + String(i) + "_";

    bool wasEnabled = inputs[i].enabled;
    int oldPin = inputs[i].pin;

    inputs[i].enabled = server.hasArg((prefix + "en").c_str());

    if (server.hasArg((prefix + "name").c_str())) {
      inputs[i].name = server.arg((prefix + "name").c_str());
    }

    if (server.hasArg((prefix + "pin").c_str())) {
      int newPin = server.arg((prefix + "pin").c_str()).toInt();
      if (newPin >= 0 && newPin <= 48) {
        if (newPin != oldPin) {
          pinMode(oldPin, INPUT);
          inputs[i].pin = newPin;
          if (inputs[i].enabled) {
            pinMode(newPin, INPUT_PULLDOWN);
            inputs[i].lastState = digitalRead(newPin);
          }
        }
      }
    }

    if (server.hasArg((prefix + "mode").c_str())) {
      inputs[i].momentary_mode = (server.arg((prefix + "mode").c_str()) == "momentary");
    }

    if (server.hasArg((prefix + "on").c_str())) {
      inputs[i].message_on = server.arg((prefix + "on").c_str());
    }

    if (server.hasArg((prefix + "off").c_str())) {
      inputs[i].message_off = server.arg((prefix + "off").c_str());
    }

    // NEW v3.0: Handle camera and GPS per input
    inputs[i].capture_photo = server.hasArg((prefix + "cam").c_str());
    inputs[i].include_gps = server.hasArg((prefix + "gps").c_str());

    // Handle enable/disable state changes
    if (!wasEnabled && inputs[i].enabled) {
      pinMode(inputs[i].pin, INPUT_PULLDOWN);
      inputs[i].lastState = digitalRead(inputs[i].pin);
    } else if (wasEnabled && !inputs[i].enabled) {
      pinMode(inputs[i].pin, INPUT);
    }
  }

  // Defer savePreferences() to prevent blocking during HTTP response
  needs_save_prefs = true;
  server.send(200, "text/html", F("<html><head><meta http-equiv='refresh' content='2;url=/'></head><body style='text-align:center;padding:50px'><h1>Saved!</h1></body></html>"));
}

void handleStatus() {
  StaticJsonDocument<768> doc;
  doc["wifi"] = WiFi.status() == WL_CONNECTED;
  doc["ip"] = WiFi.localIP().toString();
  doc["uptime"] = millis();
  doc["heap"] = ESP.getFreeHeap();
  doc["psram"] = ESP.getFreePsram();

  // Cellular status
  if (board_type == BOARD_SIM7670G) {
    JsonObject cellular = doc.createNestedObject("cellular");
    cellular["enabled"] = cellular_enabled;
    cellular["connected"] = cellular_connected;
    cellular["operator"] = cellular_operator;
    cellular["signal"] = cellular_signal_strength;
  }

  JsonArray inputsArray = doc.createNestedArray("inputs");
  for (int i = 0; i < MAX_INPUTS; i++) {
    if (inputs[i].enabled) {
      inputsArray.add(digitalRead(inputs[i].pin) == HIGH);
    } else {
      inputsArray.add(false);
    }
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleRestart() {
  server.send(200, "text/html", F("<html><body><h2>Restarting...</h2></body></html>"));
  delay(1000);
  ESP.restart();
}

void handleTestPushbullet() {
  if (pushbullet_token.length() > 0) {
    bool success = sendPushbulletNotification("Test", "Pushbullet test notification from Notifier v" VERSION);
    server.send(200, "text/plain", success ? "SUCCESS" : "FAILED");
  } else {
    server.send(400, "text/plain", "No token configured");
  }
}

void handleTestEmail() {
  if (smtp_email.length() > 0 && recipient_email.length() > 0) {
    bool success = sendEmailNotification("Test", "Email test notification from Notifier v" VERSION);
    server.send(200, "text/plain", success ? "SUCCESS" : "FAILED");
  } else {
    server.send(400, "text/plain", "Email not configured");
  }
}

void handleTestTelegram() {
  if (telegram_token.length() > 0 && telegram_chat_id.length() > 0) {
    bool success = sendTelegramNotification("Telegram test notification from Notifier v" VERSION);
    server.send(200, "text/plain", success ? "SUCCESS" : "FAILED");
  } else {
    server.send(400, "text/plain", "Telegram not configured");
  }
}

void handleTestCamera() {
  if (!camera_enabled || !camera_initialized) {
    server.send(400, "text/plain", "Camera not enabled or initialized");
    return;
  }

  String filename = capturePhoto();
  if (filename.length() > 0) {
    server.send(200, "text/plain", "SUCCESS - Photo saved: " + filename);
  } else {
    server.send(500, "text/plain", "FAILED - Could not capture photo");
  }
}

void handleTestInput() {
  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Missing input ID parameter");
    return;
  }

  int inputId = server.arg("id").toInt();
  if (inputId < 0 || inputId >= MAX_INPUTS) {
    server.send(400, "text/plain", "Invalid input ID");
    return;
  }

  if (!inputs[inputId].enabled) {
    server.send(400, "text/plain", "Input " + String(inputId + 1) + " is not enabled");
    return;
  }

  // Simulate input trigger
  Serial.printf("Manual test triggered for Input %d (%s)\n", inputId + 1, inputs[inputId].name.c_str());
  addLog("INFO", "Manual trigger test: Input " + String(inputId + 1));

  // Get current timestamp
  struct tm timeinfo;
  String timestamp = "Unknown time";
  if (getLocalTime(&timeinfo)) {
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    timestamp = String(buffer);
  }

  // Use ON message for manual test
  String message = inputs[inputId].message_on;
  message.replace("{name}", inputs[inputId].name);
  message.replace("{timestamp}", timestamp);

  // Capture photo if enabled for this input
  String photoFilename = "";
  if (inputs[inputId].capture_photo && camera_enabled && camera_initialized) {
    photoFilename = capturePhoto();
  }

  // Send notifications
  bool anySuccess = false;
  if (pushbullet_enabled && pushbullet_token.length() > 0) {
    anySuccess |= sendPushbulletNotification(notification_title, message);
  }
  if (email_enabled && recipient_email.length() > 0) {
    anySuccess |= sendEmailNotification(notification_title, message);
  }
  if (telegram_enabled && telegram_token.length() > 0) {
    anySuccess |= sendTelegramNotification(message);
  }

  String response = "Test notification sent for Input " + String(inputId + 1) + " (" + inputs[inputId].name + ")";
  if (photoFilename.length() > 0) {
    response += "\nPhoto captured: " + photoFilename;
  }
  if (anySuccess) {
    response += "\nNotifications: SUCCESS";
  } else {
    response += "\nNotifications: No services enabled or configured";
  }

  server.send(200, "text/plain", response);
}

void handleTestSMS() {
  if (!cellular_enabled || !modem_initialized) {
    server.send(400, "text/plain", "Cellular modem not enabled or initialized");
    return;
  }

  if (!sms_enabled || sms_phone_number.length() == 0) {
    server.send(400, "text/plain", "SMS not configured");
    return;
  }

  String testMessage = "SMS test notification from ESP32-Notifier v" VERSION;
  bool success = sendSMS(sms_phone_number, testMessage);

  if (success) {
    server.send(200, "text/plain", "SUCCESS - SMS sent to " + sms_phone_number);
  } else {
    server.send(500, "text/plain", "FAILED - Could not send SMS");
  }
}

void handleTestBMP180() {
  if (!bmp180_enabled) {
    server.send(400, "text/plain", "BMP180 sensor not enabled");
    return;
  }

  if (!bmp180_initialized) {
    server.send(400, "text/plain", "BMP180 sensor not initialized - check wiring");
    return;
  }

  // Get fresh sensor readings
  updateBMP180();

  String data = "BMP180 Sensor Readings:\n";
  data += "Temperature: " + String(last_temperature, 1) + "°C / " + String(last_temperature * 9.0 / 5.0 + 32.0, 1) + "°F\n";
  data += "Pressure: " + String(last_pressure, 1) + " hPa / " + String(last_pressure * 0.02953, 2) + " inHg\n";
  data += "Altitude: " + String(last_altitude, 1) + " m / " + String(last_altitude * 3.28084, 1) + " ft\n";
  data += "I2C Pins: SDA=GPIO" + String(bmp180_sda_pin) + ", SCL=GPIO" + String(bmp180_scl_pin);

  server.send(200, "text/plain", data);
}

void handleSearchBMP180() {
  // Scan I2C bus for BMP180 sensor at address 0x77
  String result = "=== I2C BUS DIAGNOSTIC SCAN ===\n\n";
  result += "Configuration:\n";
  result += "  SDA Pin: GPIO" + String(bmp180_sda_pin) + "\n";
  result += "  SCL Pin: GPIO" + String(bmp180_scl_pin) + "\n";
  result += "  Clock Speed: 100kHz\n";
  result += "  Pull-ups: Internal (enabled)\n\n";

  // Properly reinitialize Wire with the configured pins
  Wire.end();
  delay(100);

  // Enable internal pull-ups
  pinMode(bmp180_sda_pin, INPUT_PULLUP);
  pinMode(bmp180_scl_pin, INPUT_PULLUP);
  delay(50);

  Wire.begin(bmp180_sda_pin, bmp180_scl_pin);
  Wire.setClock(100000);  // 100kHz for better reliability during scan
  delay(100);

  result += "Scanning I2C addresses (0x01 - 0x7F)...\n";
  result += "----------------------------------------\n";

  bool found = false;
  int deviceCount = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      deviceCount++;
      result += "0x";
      if (addr < 16) result += "0";  // Add leading zero for single hex digit
      result += String(addr, HEX);
      result += " - ";

      if (addr == 0x77) {
        result += "BMP180/BMP085 FOUND!";
        found = true;
      } else if (addr == 0x76) {
        result += "BME280/BMP280 (wrong sensor!)";
      } else {
        result += "Unknown device";
      }
      result += "\n";
    }
  }

  result += "----------------------------------------\n";
  result += "Total I2C devices found: " + String(deviceCount) + "\n\n";

  if (!found) {
    if (deviceCount > 0) {
      result += "STATUS: I2C bus is working, but no BMP180!\n";
      result += "The sensor at 0x77 is NOT a BMP180.\n";
      result += "If you see 0x76, that's a BME280/BMP280 (different sensor).\n";
    } else {
      result += "STATUS: NO I2C devices detected!\n";
      result += "This indicates a WIRING PROBLEM.\n\n";
      result += "TROUBLESHOOTING CHECKLIST:\n";
      result += "[ ] VCC connected to 3.3V (NOT 5V!)\n";
      result += "[ ] GND connected to GND\n";
      result += "[ ] SDA wire connected to GPIO" + String(bmp180_sda_pin) + "\n";
      result += "[ ] SCL wire connected to GPIO" + String(bmp180_scl_pin) + "\n";
      result += "[ ] All connections are TIGHT and SECURE\n";
      result += "[ ] Not using a damaged breadboard/wires\n";
      result += "[ ] Sensor module has power LED lit (if present)\n\n";
      result += "THINGS TO TRY:\n";
      result += "1. Swap SDA and SCL wires (labels might be wrong)\n";
      result += "2. Try different GPIO pins (e.g., GPIO21/22)\n";
      result += "3. Use shorter wires (< 10cm)\n";
      result += "4. Test with a different BMP180 module\n";
      result += "5. Measure voltage: VCC should be 3.3V\n";
      result += "6. Check sensor with multimeter for shorts\n";
    }
  } else {
    result += "SUCCESS: BMP180 sensor detected!\n";
    result += "The sensor is communicating properly.\n";
    result += "You can now enable it in the settings.\n";
  }

  server.send(200, "text/plain", result);
}

void handleTestGPIO() {
  // Test GPIO pins for I2C - helps diagnose hardware issues
  String result = "=== GPIO PIN DIAGNOSTIC ===\n\n";
  result += "Testing GPIO " + String(bmp180_sda_pin) + " (SDA) and GPIO " + String(bmp180_scl_pin) + " (SCL)\n\n";

  // Test 1: Check if pins can be set as inputs
  pinMode(bmp180_sda_pin, INPUT);
  pinMode(bmp180_scl_pin, INPUT);
  delay(10);
  int sda_read = digitalRead(bmp180_sda_pin);
  int scl_read = digitalRead(bmp180_scl_pin);

  result += "Test 1 - INPUT mode (no pull-up):\n";
  result += "  SDA (GPIO" + String(bmp180_sda_pin) + "): " + String(sda_read) + "\n";
  result += "  SCL (GPIO" + String(bmp180_scl_pin) + "): " + String(scl_read) + "\n\n";

  // Test 2: Enable pull-ups and read again
  pinMode(bmp180_sda_pin, INPUT_PULLUP);
  pinMode(bmp180_scl_pin, INPUT_PULLUP);
  delay(10);
  int sda_pullup = digitalRead(bmp180_sda_pin);
  int scl_pullup = digitalRead(bmp180_scl_pin);

  result += "Test 2 - INPUT_PULLUP mode:\n";
  result += "  SDA (GPIO" + String(bmp180_sda_pin) + "): " + String(sda_pullup);
  if (sda_pullup == HIGH) result += " ✓ (Good)\n";
  else result += " ✗ (Should be HIGH - pin may be shorted to GND!)\n";

  result += "  SCL (GPIO" + String(bmp180_scl_pin) + "): " + String(scl_pullup);
  if (scl_pullup == HIGH) result += " ✓ (Good)\n";
  else result += " ✗ (Should be HIGH - pin may be shorted to GND!)\n";

  result += "\nDIAGNOSIS:\n";
  if (sda_pullup == LOW || scl_pullup == LOW) {
    result += "⚠️  WARNING: One or both pins read LOW with pull-up enabled!\n";
    result += "This usually means:\n";
    result += "  1. Wire is shorted to GND\n";
    result += "  2. Sensor module is damaged/shorted\n";
    result += "  3. Wrong pin number (pin doesn't exist)\n\n";
    result += "ACTION: Disconnect the BMP180 and run this test again.\n";
    result += "If pins read HIGH when disconnected, the sensor is faulty.\n";
  } else {
    result += "✓ GPIO pins appear to be working correctly.\n";
    result += "The problem is likely:\n";
    result += "  1. BMP180 module not receiving power (check 3.3V connection)\n";
    result += "  2. Faulty BMP180 sensor chip\n";
    result += "  3. Incorrect wiring (check connections carefully)\n\n";
    result += "NEXT STEPS:\n";
    result += "  1. Measure voltage at BMP180 VCC pin (should be 3.3V)\n";
    result += "  2. Check if sensor has a power LED (should be lit)\n";
    result += "  3. Try sensor on a different ESP32 board\n";
    result += "  4. The sensor modules might both be defective\n";
  }

  server.send(200, "text/plain", result);
}

void handleTestCellular() {
  if (!cellular_enabled) {
    server.send(400, "text/plain", "Cellular modem not enabled");
    return;
  }

  if (!modem_initialized) {
    server.send(400, "text/plain", "Cellular modem not initialized");
    return;
  }

  String result = "Cellular Modem Test:\n\n";
  result += "Model: " + modem_model + "\n";
  result += "IMEI: " + modem_imei + "\n";
  result += "ICCID: " + modem_iccid + "\n";
  result += "Network: " + cellular_operator + "\n";
  result += "Signal: " + String(cellular_signal_strength) + "\n";
  result += "Connected: " + String(cellular_connected ? "Yes" : "No") + "\n";
  result += "APN: " + apn + "\n\n";

  // Test AT command
  String atResponse = sendATCommand("AT", 1000);
  if (atResponse.indexOf("OK") >= 0) {
    result += "AT Command: OK\n";
  } else {
    result += "AT Command: FAILED\n";
  }

  // Test signal quality
  String signalResponse = sendATCommand("AT+CSQ", 1000);
  if (signalResponse.length() > 0) {
    result += "Signal Response: " + signalResponse.substring(0, 50) + "\n";
  }

  server.send(200, "text/plain", result);
}

void handleResetWiFi() {
  String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Resetting WiFi...</title><style>"
  "body{font-family:Arial;margin:20px;background:#f0f0f0;text-align:center}"
  ".container{max-width:600px;margin:auto;background:#fff;padding:40px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
  "h1{color:#f44336}p{color:#666;line-height:1.6}"
  "</style></head><body><div class='container'>"
  "<h1>WiFi Settings Reset</h1>"
  "<p>WiFi credentials have been cleared.</p>"
  "<p>The device will restart in Access Point mode.</p>"
  "<p><strong>Connect to:</strong> ESP32-Notifier-Setup</p>"
  "<p><strong>Password:</strong> setup123</p>"
  "<p><strong>Setup URL:</strong> http://192.168.4.1</p>"
  "<p>This window will close automatically...</p>"
  "</div><script>setTimeout(function(){window.close()},3000);</script></body></html>");

  // Clear WiFi credentials
  wifi_ssid = "";
  wifi_password = "";

  // Save empty credentials
  savePreferences();

  addLog("INFO", "WiFi credentials reset - restarting in AP mode");

  server.send(200, "text/html", html);

  delay(2000);
  ESP.restart();
}

void handleGetConfig() {
  StaticJsonDocument<2048> doc;

  doc["version"] = VERSION;
  doc["wifi_ssid"] = wifi_ssid;
  doc["web_username"] = web_username;
  doc["board_type"] = board_type;
  doc["camera_enabled"] = camera_enabled;
  doc["gps_enabled"] = gps_enabled;
  doc["cellular_enabled"] = cellular_enabled;
  doc["apn"] = apn;

  doc["pushbullet_enabled"] = pushbullet_enabled;
  doc["pushbullet_token"] = pushbullet_token;
  doc["pushbullet_conn_mode"] = pushbullet_conn_mode;

  doc["email_enabled"] = email_enabled;
  doc["smtp_host"] = smtp_host;
  doc["smtp_port"] = smtp_port;
  doc["smtp_email"] = smtp_email;
  doc["recipient_email"] = recipient_email;
  doc["email_conn_mode"] = email_conn_mode;

  doc["telegram_enabled"] = telegram_enabled;
  doc["telegram_token"] = telegram_token;
  doc["telegram_chat_id"] = telegram_chat_id;
  doc["telegram_conn_mode"] = telegram_conn_mode;

  doc["sms_enabled"] = sms_enabled;
  doc["sms_phone_number"] = sms_phone_number;
  doc["sms_conn_mode"] = sms_conn_mode;

  doc["notification_title"] = notification_title;
  doc["gmt_offset"] = gmt_offset;
  doc["daylight_offset"] = daylight_offset;

  JsonArray inputsArray = doc.createNestedArray("inputs");
  for (int i = 0; i < MAX_INPUTS; i++) {
    JsonObject input = inputsArray.createNestedObject();
    input["enabled"] = inputs[i].enabled;
    input["name"] = inputs[i].name;
    input["pin"] = inputs[i].pin;
    input["momentary_mode"] = inputs[i].momentary_mode;
    input["capture_photo"] = inputs[i].capture_photo;
    input["include_gps"] = inputs[i].include_gps;
    input["message_on"] = inputs[i].message_on;
    input["message_off"] = inputs[i].message_off;
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleGallery() {
  #ifdef HAS_CAMERA_LIB
  if (!camera_enabled || !sd_card_available) {
    server.send(400, "text/plain", "Camera or SD card not available");
    return;
  }

  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Photo Gallery - ESP32-Notifier</title><style>"
  ":root{--bg:#f0f0f0;--card:#fff;--text:#333;--border:#ddd}"
  "body.dark{--bg:#1a1a1a;--card:#2d2d2d;--text:#e0e0e0;--border:#404040}"
  "body{font-family:Arial;margin:20px;background:var(--bg);color:var(--text);transition:all 0.3s}"
  ".c{max-width:1200px;margin:auto;background:var(--card);padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
  "h1{display:flex;justify-content:space-between;align-items:center}"
  ".theme-toggle{background:none;border:2px solid var(--border);padding:6px 12px;cursor:pointer;border-radius:4px;font-size:18px}"
  ".back{display:inline-block;background:#4CAF50;color:#fff;padding:8px 16px;text-decoration:none;border-radius:4px;margin-bottom:10px}"
  ".gallery{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:15px;margin-top:20px}"
  ".photo-card{background:var(--card);border:1px solid var(--border);border-radius:8px;overflow:hidden;transition:transform 0.2s}"
  ".photo-card:hover{transform:scale(1.02);box-shadow:0 4px 15px rgba(0,0,0,0.2)}"
  ".photo-card img{width:100%;height:200px;object-fit:cover;background:#000}"
  ".photo-info{padding:10px;font-size:12px}"
  ".photo-name{font-weight:bold;margin-bottom:5px}"
  ".photo-size{color:#666}"
  ".btn{display:inline-block;padding:6px 12px;margin:5px 2px;background:#2196F3;color:#fff;text-decoration:none;border-radius:4px;font-size:12px}"
  ".btn-danger{background:#f44336}"
  "#loading{text-align:center;padding:40px;color:var(--text)}"
  "</style></head><body><div class='c'><a href='/' class='back'>← Back</a>"
  "<h1><span>📷 Photo Gallery</span><button class='theme-toggle' onclick='toggleTheme()'>🌓</button></h1>"
  "<div id='loading'>Loading photos...</div><div id='gallery' class='gallery'></div></div>"
  "<script>"
  "function toggleTheme(){document.body.classList.toggle('dark');localStorage.setItem('theme',document.body.classList.contains('dark')?'dark':'light')}"
  "if(localStorage.getItem('theme')==='dark')document.body.classList.add('dark');"
  "async function loadGallery(){"
  "try{"
  "const r=await fetch('/api/photos');"
  "const d=await r.json();"
  "document.getElementById('loading').style.display='none';"
  "if(d.photos.length===0){document.getElementById('gallery').innerHTML='<p>No photos found</p>';return}"
  "let html='';"
  "for(const p of d.photos){"
  "html+='<div class=\"photo-card\">';"
  "html+='<img src=\"/photo?file='+p.name+'\" alt=\"'+p.name+'\">';"
  "html+='<div class=\"photo-info\">';"
  "html+='<div class=\"photo-name\">'+p.name+'</div>';"
  "html+='<div class=\"photo-size\">'+Math.round(p.size/1024)+' KB</div>';"
  "html+='<a href=\"/photo?file='+p.name+'&download=1\" class=\"btn\">Download</a>';"
  "html+='</div></div>';"
  "}"
  "document.getElementById('gallery').innerHTML=html;"
  "}catch(e){document.getElementById('loading').innerHTML='Error loading photos: '+e}"
  "}"
  "loadGallery();"
  "</script></body></html>");

  server.send(200, "text/html", html);
  #else
  server.send(400, "text/plain", "Camera library not available");
  #endif
}

void handlePhotoList() {
  #ifdef HAS_CAMERA_LIB
  if (!sd_card_available) {
    server.send(400, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }

  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) {
    server.send(500, "application/json", "{\"error\":\"Failed to open root directory\"}");
    return;
  }

  String json = "{\"photos\":[";
  bool first = true;

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      // Remove leading slash if present
      if (filename.startsWith("/")) {
        filename = filename.substring(1);
      }
      if (filename.endsWith(".jpg") || filename.endsWith(".JPG")) {
        if (!first) json += ",";
        json += "{\"name\":\"" + filename + "\",\"size\":" + String(file.size()) + "}";
        first = false;
      }
    }
    file = root.openNextFile();
  }

  json += "]}";
  server.send(200, "application/json", json);
  #else
  server.send(400, "application/json", "{\"error\":\"Camera library not available\"}");
  #endif
}

void handlePhotoDownload() {
  #ifdef HAS_CAMERA_LIB
  if (!sd_card_available) {
    server.send(400, "text/plain", "SD card not available");
    return;
  }

  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }

  String filename = server.arg("file");
  if (!filename.startsWith("/")) filename = "/" + filename;

  File file = SD_MMC.open(filename.c_str(), FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  bool download = server.hasArg("download");
  if (download) {
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + String(file.name()) + "\"");
  }

  server.streamFile(file, "image/jpeg");
  file.close();
  #else
  server.send(400, "text/plain", "Camera library not available");
  #endif
}

void handleCellularDiagnostics() {
  if (board_type != BOARD_SIM7670G) {
    server.send(400, "text/plain", "Board does not support cellular");
    return;
  }

  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Cellular Diagnostics - ESP32-Notifier</title><style>"
  ":root{--bg:#f0f0f0;--card:#fff;--text:#333;--text2:#555;--border:#ddd}"
  "body.dark{--bg:#1a1a1a;--card:#2d2d2d;--text:#e0e0e0;--text2:#b0b0b0;--border:#404040}"
  "body{font-family:Arial;margin:20px;background:var(--bg);color:var(--text);transition:all 0.3s}"
  ".c{max-width:900px;margin:auto;background:var(--card);padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
  "h1{display:flex;justify-content:space-between;align-items:center}"
  ".theme-toggle{background:none;border:2px solid var(--border);padding:6px 12px;cursor:pointer;border-radius:4px;font-size:18px}"
  ".back{display:inline-block;background:#4CAF50;color:#fff;padding:8px 16px;text-decoration:none;border-radius:4px;margin-bottom:10px}"
  ".info-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:15px;margin:20px 0}"
  ".info-card{background:var(--card);border:1px solid var(--border);padding:15px;border-radius:8px}"
  ".info-card h3{margin:0 0 10px 0;color:var(--text2);font-size:14px;text-transform:uppercase}"
  ".info-value{font-size:24px;font-weight:bold;margin:5px 0}"
  ".status-badge{display:inline-block;padding:4px 12px;border-radius:12px;font-size:12px;font-weight:bold}"
  ".status-connected{background:#4CAF50;color:#fff}.status-disconnected{background:#f44336;color:#fff}.status-disabled{background:#999;color:#fff}"
  ".signal-meter{width:100%;height:30px;background:var(--border);border-radius:4px;overflow:hidden;margin:10px 0}"
  ".signal-fill{height:100%;transition:width 0.5s}"
  ".sig-excellent{background:#4CAF50}.sig-good{background:#8BC34A}.sig-fair{background:#FF9800}.sig-poor{background:#f44336}"
  "button{background:#2196F3;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;margin:5px;font-size:14px}"
  "button:hover{opacity:0.9}.btn-danger{background:#f44336}"
  "#loading{text-align:center;padding:20px;color:var(--text2)}"
  ".detail-table{width:100%;border-collapse:collapse;margin:15px 0}"
  ".detail-table td{padding:8px;border-bottom:1px solid var(--border)}"
  ".detail-table td:first-child{font-weight:bold;width:40%;color:var(--text2)}"
  "</style></head><body><div class='c'><a href='/' class='back'>← Back</a>"
  "<h1><span>📶 Cellular Diagnostics</span><button class='theme-toggle' onclick='toggleTheme()'>🌓</button></h1>"
  "<div id='loading'>Loading diagnostics...</div><div id='content' style='display:none'>"
  "<div class='info-grid'>"
  "<div class='info-card'><h3>Connection Status</h3><div id='status'></div></div>"
  "<div class='info-card'><h3>Signal Strength</h3><div id='signal'></div></div>"
  "<div class='info-card'><h3>Network Type</h3><div class='info-value' id='network'></div></div>"
  "<div class='info-card'><h3>Operator</h3><div class='info-value' id='operator'></div></div>"
  "</div>"
  "<h2>Modem Information</h2><table class='detail-table'>"
  "<tr><td>Model</td><td id='model'>-</td></tr>"
  "<tr><td>IMEI</td><td id='imei'>-</td></tr>"
  "<tr><td>SIM ICCID</td><td id='iccid'>-</td></tr>"
  "<tr><td>APN</td><td id='apn'>-</td></tr>"
  "</table>"
  "<h2>Actions</h2>"
  "<button onclick='refreshData()'>🔄 Refresh Status</button>"
  "<button onclick='reconnect()' class='btn-danger'>🔌 Reconnect</button>"
  "</div></div>"
  "<script>"
  "function toggleTheme(){document.body.classList.toggle('dark');localStorage.setItem('theme',document.body.classList.contains('dark')?'dark':'light')}"
  "if(localStorage.getItem('theme')==='dark')document.body.classList.add('dark');"
  "function loadData(){fetch('/api/cellular').then(r=>r.json()).then(d=>{"
  "document.getElementById('loading').style.display='none';"
  "document.getElementById('content').style.display='block';"
  "let statusHtml='<div class=\"status-badge status-'+(d.connected?'connected':'disconnected')+'\">';"
  "statusHtml+=d.connected?'Connected':'Disconnected';"
  "statusHtml+='</div>';document.getElementById('status').innerHTML=statusHtml;"
  "let sigPct=Math.min(100,Math.round((d.signal/31)*100));"
  "let sigClass=sigPct>75?'excellent':sigPct>50?'good':sigPct>25?'fair':'poor';"
  "let sigHtml='<div class=\"info-value\">'+d.signal+'/31 ('+sigPct+'%)</div>';"
  "sigHtml+='<div class=\"signal-meter\"><div class=\"signal-fill sig-'+sigClass+'\" style=\"width:'+sigPct+'%\"></div></div>';"
  "document.getElementById('signal').innerHTML=sigHtml;"
  "document.getElementById('network').textContent=d.network_type||'Unknown';"
  "document.getElementById('operator').textContent=d.operator||'None';"
  "document.getElementById('model').textContent=d.model||'Unknown';"
  "document.getElementById('imei').textContent=d.imei||'Unknown';"
  "document.getElementById('iccid').textContent=d.iccid||'No SIM';"
  "document.getElementById('apn').textContent=d.apn||'Not set';"
  "}).catch(e=>{document.getElementById('loading').innerHTML='Error loading data: '+e})}"
  "function refreshData(){document.getElementById('loading').style.display='block';document.getElementById('content').style.display='none';"
  "fetch('/cellular/refresh').then(()=>setTimeout(loadData,2000)).catch(e=>alert('Refresh failed: '+e))}"
  "function reconnect(){if(confirm('Reconnect to cellular network? This may take 30-60 seconds.')){alert('Reconnecting... Please wait.'); refreshData()}}"
  "loadData();"
  "</script></body></html>");

  server.send(200, "text/html", html);
}

void handleCellularAPI() {
  StaticJsonDocument<512> doc;

  doc["enabled"] = cellular_enabled;
  doc["initialized"] = modem_initialized;
  doc["connected"] = cellular_connected;
  doc["operator"] = cellular_operator;
  doc["signal"] = cellular_signal_strength;
  doc["network_type"] = modem_network_type;
  doc["model"] = modem_model;
  doc["imei"] = modem_imei;
  doc["iccid"] = modem_iccid;
  doc["apn"] = apn;

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleCellularRefresh() {
  if (!cellular_enabled || !modem_initialized) {
    server.send(400, "text/plain", "Cellular not enabled or modem not initialized");
    return;
  }

  // Refresh signal strength
  String response = sendATCommand("AT+CSQ");
  int rssiStart = response.indexOf(": ") + 2;
  int rssiEnd = response.indexOf(",", rssiStart);
  if (rssiStart > 1 && rssiEnd > rssiStart) {
    cellular_signal_strength = response.substring(rssiStart, rssiEnd).toInt();
  }

  // Refresh network type
  response = sendATCommand("AT+CPSI?");
  if (response.indexOf("LTE") >= 0) {
    modem_network_type = "4G LTE";
  } else if (response.indexOf("WCDMA") >= 0 || response.indexOf("HSDPA") >= 0) {
    modem_network_type = "3G";
  } else if (response.indexOf("GSM") >= 0 || response.indexOf("GPRS") >= 0) {
    modem_network_type = "2G";
  }

  // Check connection status
  response = sendATCommand("AT+CGACT?");
  cellular_connected = (response.indexOf("+CGACT: 1,1") >= 0);

  server.send(200, "text/plain", "Refreshed");
}

// ========================================
// CONNECTION HELPER FUNCTIONS
// ========================================

bool shouldUseCellular(int connectionMode) {
  // Determine if we should use cellular based on mode and availability
  switch (connectionMode) {
    case CONN_WIFI_ONLY:
      return false;
    case CONN_CELL_ONLY:
      return cellular_connected;
    case CONN_WIFI_CELL_BACKUP:
      return (wifiState != WIFI_CONNECTED && cellular_connected);
    default:
      return false;
  }
}

String getConnectionStatus() {
  String status = "";
  if (wifiState == WIFI_CONNECTED) {
    status += "WiFi: Connected";
  } else {
    status += "WiFi: Disconnected";
  }

  if (cellular_connected) {
    status += " | Cell: " + cellular_operator + " (RSSI: " + String(cellular_signal_strength) + ")";
  } else if (cellular_enabled) {
    status += " | Cell: Disconnected";
  }

  return status;
}

void sendNotifications(String title, String body, String photoFile) {
  Serial.println(F("--- Sending Notifications ---"));
  Serial.println(getConnectionStatus());

  // Add BMP180 sensor data if enabled and configured
  if (include_bmp180_in_notifications && bmp180_initialized) {
    String sensorData = getBMP180DataFormatted();
    if (sensorData.length() > 0) {
      body += "\n" + sensorData;
      Serial.println(F("Added BMP180 sensor data to notification"));
    }
  }

  // Pushbullet
  if (pushbullet_enabled && pushbullet_token.length() > 0) {
    bool useCellular = shouldUseCellular(pushbullet_conn_mode);
    if (useCellular) {
      // Send via cellular HTTP
      StaticJsonDocument<256> doc;
      doc["type"] = "note";
      doc["title"] = title;
      doc["body"] = body;
      String payload;
      serializeJson(doc, payload);

      String response = sendHTTPRequest("https://api.pushbullet.com/v2/pushes", "POST", payload);
      if (response.indexOf("200") >= 0 || response.indexOf("OK") >= 0) {
        Serial.println(F("✓ Pushbullet sent (cellular)"));
        addLog("SUCCESS", "Pushbullet sent via cellular");
      } else {
        addLog("ERROR", "Pushbullet failed via cellular");
      }
    } else if (wifiState == WIFI_CONNECTED) {
      if (!sendPushbulletNotification(title, body, photoFile)) {
        queueRetry("pushbullet", title, body);
      }
    }
  }

  // Email
  if (email_enabled && smtp_email.length() > 0 && recipient_email.length() > 0) {
    bool useCellular = shouldUseCellular(email_conn_mode);
    // Email over cellular would require complex SMTP over AT commands
    // For now, only send via WiFi
    if (!useCellular && wifiState == WIFI_CONNECTED) {
      if (!sendEmailNotification(title, body, photoFile)) {
        queueRetry("email", title, body);
      }
    } else if (useCellular) {
      Serial.println(F("Email over cellular not yet implemented, using WiFi fallback"));
      if (wifiState == WIFI_CONNECTED) {
        sendEmailNotification(title, body, photoFile);
      }
    }
  }

  // Telegram
  if (telegram_enabled && telegram_token.length() > 0 && telegram_chat_id.length() > 0) {
    bool useCellular = shouldUseCellular(telegram_conn_mode);
    if (useCellular) {
      // Send via cellular HTTP
      StaticJsonDocument<512> doc;
      doc["chat_id"] = telegram_chat_id;
      doc["text"] = body;
      String payload;
      serializeJson(doc, payload);

      String url = "https://api.telegram.org/bot" + telegram_token + "/sendMessage";
      String response = sendHTTPRequest(url, "POST", payload);
      if (response.indexOf("200") >= 0 || response.indexOf("\"ok\":true") >= 0) {
        Serial.println(F("✓ Telegram sent (cellular)"));
        addLog("SUCCESS", "Telegram sent via cellular");
      } else {
        addLog("ERROR", "Telegram failed via cellular");
      }
    } else if (wifiState == WIFI_CONNECTED) {
      if (!sendTelegramNotification(body, photoFile)) {
        queueRetry("telegram", title, body);
      }
    }
  }

  // SMS
  if (sms_enabled && sms_phone_number.length() > 0) {
    bool useCellular = shouldUseCellular(sms_conn_mode);
    if (useCellular || cellular_connected) {
      // Truncate message for SMS (160 char limit)
      String smsBody = body;
      if (smsBody.length() > 160) {
        smsBody = smsBody.substring(0, 157) + "...";
      }
      sendSMS(sms_phone_number, smsBody);
    }
  }

  Serial.println(F("--- Notifications Complete ---"));
}

void handleLogs() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<meta http-equiv='refresh' content='10'><title>Logs - ESP32-Notifier</title><style>"
  "body{font-family:monospace;margin:20px;background:#1e1e1e;color:#d4d4d4}.c{max-width:1000px;margin:auto}"
  "h1{color:#4ec9b0;text-align:center}.log{margin:5px 0;padding:8px;border-radius:4px;font-size:12px;line-height:1.4}"
  ".INFO{background:#1e3a5f;border-left:4px solid #4fc3f7}"
  ".SUCCESS{background:#1e3a1e;border-left:4px solid #4caf50}"
  ".WARNING{background:#3a2e1e;border-left:4px solid #ff9800}"
  ".ERROR{background:#3a1e1e;border-left:4px solid #f44336}"
  ".time{color:#858585;margin-right:10px}.level{font-weight:bold;margin-right:10px}"
  ".back{display:inline-block;background:#4CAF50;color:#fff;padding:8px 16px;text-decoration:none;border-radius:4px;margin-bottom:10px}"
  "</style></head><body><div class='c'><a href='/' class='back'>← Back</a>"
  "<h1>System Logs</h1>");

  // Display logs in reverse order (newest first)
  int displayCount = (logCount < MAX_LOG_ENTRIES) ? logCount : MAX_LOG_ENTRIES;
  for (int i = displayCount - 1; i >= 0; i--) {
    int idx = (logIndex - 1 - i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    LogEntry &entry = logBuffer[idx];

    html += "<div class='log " + entry.level + "'>";
    html += "<span class='time'>" + htmlEncode(entry.timestamp) + "</span>";
    html += "<span class='level'>" + entry.level + "</span>";
    html += htmlEncode(entry.message);
    html += "</div>";
  }

  html += F("</div></body></html>");
  server.send(200, "text/html", html);
}

bool sendPushbulletNotification(String title, String body, String photoFile) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("✗ Pushbullet: WiFi down"));
    return false;
  }

  HTTPClient http;
  http.begin("https://api.pushbullet.com/v2/pushes");
  http.addHeader("Access-Token", pushbullet_token);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;

  // If photo is available, send as file URL pointing to ESP32 web server
  if (photoFile.length() > 0 && sd_card_available) {
    // Get ESP32's IP address
    String ipAddr = WiFi.localIP().toString();
    String filename = photoFile;
    if (filename.startsWith("/")) filename = filename.substring(1);

    // Create photo URL accessible from the internet (if port forwarded)
    // Or accessible from local network
    String photoUrl = "http://" + ipAddr + "/photo?file=" + filename;

    doc["type"] = "link";
    doc["title"] = title;
    doc["body"] = body;
    doc["url"] = photoUrl;

    Serial.print(F("Pushbullet: Sending with photo link: "));
    Serial.println(photoUrl);
  } else {
    // No photo, send as regular note
    doc["type"] = "note";
    doc["title"] = title;
    doc["body"] = body;
  }

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  http.end();

  if (code >= 200 && code < 300) {
    Serial.println(F("✓ Pushbullet sent"));
    addLog("SUCCESS", "Pushbullet notification sent");
    return true;
  } else {
    Serial.print(F("✗ Pushbullet failed: "));
    Serial.println(code);
    addLog("ERROR", "Pushbullet failed - HTTP " + String(code));
    return false;
  }
}

bool sendEmailNotification(String subject, String body, String photoFile) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("✗ Email: WiFi down"));
    return false;
  }

  ESP_Mail_Session session;
  session.server.host_name = smtp_host.c_str();
  session.server.port = smtp_port;
  session.login.email = smtp_email.c_str();
  session.login.password = smtp_password.c_str();

  SMTP_Message message;
  message.sender.name = "ESP32 Notifier";
  message.sender.email = smtp_email.c_str();
  message.subject = subject;
  message.addRecipient("", recipient_email.c_str());
  message.text.content = body.c_str();

  // Attach photo if available
  if (photoFile.length() > 0 && sd_card_available) {
    SMTP_Attachment att;
    att.descr.filename = photoFile.substring(photoFile.lastIndexOf('/') + 1);
    att.descr.mime = "image/jpeg";
    att.descr.transfer_encoding = Content_Transfer_Encoding::enc_base64;
    att.file.storage_type = esp_mail_file_storage_type_sd;
    att.file.path = photoFile.c_str();
    message.addAttachment(att);
    Serial.print(F("Email: Attaching photo "));
    Serial.println(photoFile);
  }

  // Initialize SMTP session if needed
  if (smtp == nullptr) {
    smtp = new SMTPSession();
  }

  if (!smtp->connect(&session)) {
    Serial.println(F("✗ Email: SMTP connect failed"));
    return false;
  }

  bool success = MailClient.sendMail(smtp, &message);
  smtp->closeSession();

  if (success) {
    Serial.println(F("✓ Email sent"));
    addLog("SUCCESS", "Email notification sent to " + recipient_email);
    return true;
  } else {
    Serial.print(F("✗ Email failed: "));
    Serial.println(smtp->errorReason().c_str());
    addLog("ERROR", "Email failed - " + String(smtp->errorReason().c_str()));
    return false;
  }
}

bool sendTelegramNotification(String message, String photoFile) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("✗ Telegram: WiFi down"));
    return false;
  }

  HTTPClient http;

  // If photo is available, send as photo with caption
  if (photoFile.length() > 0 && sd_card_available) {
    #ifdef HAS_CAMERA_LIB
    Serial.println(F("Telegram: Sending photo..."));

    // Open the photo file
    File file = SD_MMC.open(photoFile, FILE_READ);
    if (!file) {
      Serial.println(F("✗ Telegram: Failed to open photo file"));
      // Fall back to text-only message
    } else {
      // Send photo using sendPhoto endpoint
      String url = "https://api.telegram.org/bot" + telegram_token + "/sendPhoto";

      // Create multipart boundary
      String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";

      http.begin(url);
      http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

      // Build multipart form data
      String head = "--" + boundary + "\r\n";
      head += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
      head += telegram_chat_id + "\r\n";
      head += "--" + boundary + "\r\n";
      head += "Content-Disposition: form-data; name=\"caption\"\r\n\r\n";
      head += message + "\r\n";
      head += "--" + boundary + "\r\n";
      head += "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n";
      head += "Content-Type: image/jpeg\r\n\r\n";

      String tail = "\r\n--" + boundary + "--\r\n";

      // Calculate content length
      size_t fileSize = file.size();
      size_t contentLength = head.length() + fileSize + tail.length();

      http.addHeader("Content-Length", String(contentLength));

      // Send request
      WiFiClient* stream = http.getStreamPtr();
      stream->print(head);

      // Send file data in chunks
      uint8_t buffer[512];
      size_t bytesRead;
      while ((bytesRead = file.read(buffer, sizeof(buffer))) > 0) {
        stream->write(buffer, bytesRead);
        esp_task_wdt_reset(); // Keep watchdog happy during upload
      }

      stream->print(tail);
      file.close();

      int code = http.GET(); // Complete the request
      http.end();

      if (code >= 200 && code < 300) {
        Serial.println(F("✓ Telegram photo sent"));
        addLog("SUCCESS", "Telegram photo sent");
        return true;
      } else {
        Serial.print(F("✗ Telegram photo failed: "));
        Serial.println(code);
        addLog("ERROR", "Telegram photo failed - HTTP " + String(code));
        // Fall through to send text-only message
      }
    }
    #endif
  }

  // Send text-only message (fallback or when no photo)
  String url = "https://api.telegram.org/bot" + telegram_token + "/sendMessage";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["chat_id"] = telegram_chat_id;
  doc["text"] = message;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  http.end();

  if (code >= 200 && code < 300) {
    Serial.println(F("✓ Telegram sent"));
    addLog("SUCCESS", "Telegram notification sent");
    return true;
  } else {
    Serial.print(F("✗ Telegram failed: "));
    Serial.println(code);
    addLog("ERROR", "Telegram failed - HTTP " + String(code));
    return false;
  }
}

void queueRetry(String service, String title, String body) {
  NotificationRetry retry;
  retry.service = service;
  retry.title = title;
  retry.body = body;
  retry.retryTime = millis() + RETRY_DELAY;
  retry.retryCount = 0;

  retryQueue.push_back(retry);
  Serial.print(F("Queued retry for: "));
  Serial.println(service);
}

void processRetryQueue() {
  for (int i = retryQueue.size() - 1; i >= 0; i--) {
    if (millis() >= retryQueue[i].retryTime) {
      Serial.print(F("Retrying "));
      Serial.print(retryQueue[i].service);
      Serial.print(F(" (attempt "));
      Serial.print(retryQueue[i].retryCount + 1);
      Serial.println(F(")"));

      bool success = false;

      if (retryQueue[i].service == "pushbullet") {
        success = sendPushbulletNotification(retryQueue[i].title, retryQueue[i].body);
      } else if (retryQueue[i].service == "email") {
        success = sendEmailNotification(retryQueue[i].title, retryQueue[i].body);
      } else if (retryQueue[i].service == "telegram") {
        success = sendTelegramNotification(retryQueue[i].body);
      }

      if (success || retryQueue[i].retryCount >= MAX_RETRIES) {
        retryQueue.erase(retryQueue.begin() + i);
      } else {
        retryQueue[i].retryCount++;
        retryQueue[i].retryTime = millis() + (RETRY_DELAY * (retryQueue[i].retryCount + 1));
      }
    }
  }
}
