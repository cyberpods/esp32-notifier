/*
 * ESP32-Notifier
 * Version: 2.0
 * Multi-channel notification system with web configuration
 *
 * New in v2.0:
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
#include <ESP_Mail_Client.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

#define VERSION "2.0"
#define MAX_INPUTS 4
#define WDT_TIMEOUT 30

// Web server
WebServer server(80);
Preferences preferences;
SMTPSession smtp;

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
  String name;
  String message_on;
  String message_off;
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  unsigned long lastNotificationTime;
};

InputConfig inputs[MAX_INPUTS] = {
  {4, true, false, "Input 1", "Input 1 ON at {timestamp}", "Input 1 OFF at {timestamp}", LOW, LOW, 0, 0},
  {5, false, false, "Input 2", "Input 2 ON at {timestamp}", "Input 2 OFF at {timestamp}", LOW, LOW, 0, 0},
  {6, false, false, "Input 3", "Input 3 ON at {timestamp}", "Input 3 OFF at {timestamp}", LOW, LOW, 0, 0},
  {7, false, false, "Input 4", "Input 4 ON at {timestamp}", "Input 4 OFF at {timestamp}", LOW, LOW, 0, 0}
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

void addLog(String level, String message) {
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
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n\n=== ESP32-Notifier v" VERSION " ==="));

  // Enable watchdog timer (new API for ESP32 Arduino Core 3.x)
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  Serial.println(F("Watchdog timer enabled (30s)"));

  loadPreferences();

  // Configure enabled input pins
  for (int i = 0; i < MAX_INPUTS; i++) {
    if (inputs[i].enabled) {
      pinMode(inputs[i].pin, INPUT_PULLDOWN);
      inputs[i].lastState = digitalRead(inputs[i].pin);
      Serial.print(F("Input "));
      Serial.print(i + 1);
      Serial.print(F(" ("));
      Serial.print(inputs[i].name);
      Serial.print(F(") on pin "));
      Serial.print(inputs[i].pin);
      Serial.print(F(": "));
      Serial.println(inputs[i].lastState ? F("HIGH") : F("LOW"));
    }
  }

  // Check if WiFi credentials are configured
  if (wifi_ssid.length() == 0) {
    startAccessPoint();
    addLog("INFO", "Started in AP mode - no WiFi credentials");
  } else {
    connectWiFiNonBlocking();
  }

  setupWebServer();
  server.begin();

  addLog("INFO", "System started - ESP32-Notifier v" VERSION);
  Serial.println(F("Setup complete"));
}

void loop() {
  esp_task_wdt_reset();

  updateWiFiConnection();

  if (wifiState == WIFI_CONNECTED) {
    if (!timeIsSynced && millis() - lastTimeSyncAttempt > TIME_SYNC_RETRY) {
      attemptTimeSync();
    }

    checkAndReconnectWiFi();
    server.handleClient();
    processRetryQueue();
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
    input.lastDebounceTime = millis();
    input.currentState = reading;
  }

  if ((millis() - input.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (input.currentState != input.lastState) {
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

          Serial.print(input.name);
          Serial.print(F(" state changed: "));
          Serial.println(message);

          addLog("INFO", "Trigger: " + input.name + " - " + (input.currentState == HIGH ? "HIGH" : "LOW"));
          sendNotifications(notification_title, message);
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
      addLog("ERROR", "WiFi connection timeout");
    }
  }
}

void checkAndReconnectWiFi() {
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
  preferences.begin("notifier", false);

  wifi_ssid = preferences.getString(PrefKeys::WIFI_SSID, wifi_ssid);
  wifi_password = preferences.getString(PrefKeys::WIFI_PASS, wifi_password);
  web_username = preferences.getString(PrefKeys::WEB_USER, web_username);
  web_password = preferences.getString(PrefKeys::WEB_PASS, web_password);

  pushbullet_token = preferences.getString(PrefKeys::PB_TOKEN, pushbullet_token);
  pushbullet_enabled = preferences.getBool(PrefKeys::PB_ENABLED, pushbullet_enabled);

  smtp_host = preferences.getString(PrefKeys::SMTP_HOST, smtp_host);
  smtp_port = preferences.getInt(PrefKeys::SMTP_PORT, smtp_port);
  smtp_email = preferences.getString(PrefKeys::SMTP_EMAIL, smtp_email);
  smtp_password = preferences.getString(PrefKeys::SMTP_PASS, smtp_password);
  recipient_email = preferences.getString(PrefKeys::RCPT_EMAIL, recipient_email);
  email_enabled = preferences.getBool(PrefKeys::EMAIL_ENABLED, email_enabled);

  telegram_token = preferences.getString(PrefKeys::TG_TOKEN, telegram_token);
  telegram_chat_id = preferences.getString(PrefKeys::TG_CHAT, telegram_chat_id);
  telegram_enabled = preferences.getBool(PrefKeys::TG_ENABLED, telegram_enabled);

  notification_title = preferences.getString(PrefKeys::NOTIF_TITLE, notification_title);
  gmt_offset = preferences.getLong(PrefKeys::GMT_OFFSET, gmt_offset);
  daylight_offset = preferences.getInt(PrefKeys::DAY_OFFSET, daylight_offset);

  // Load input configurations
  for (int i = 0; i < MAX_INPUTS; i++) {
    String prefix = "in" + String(i) + "_";
    inputs[i].enabled = preferences.getBool((prefix + "en").c_str(), inputs[i].enabled);
    inputs[i].pin = preferences.getInt((prefix + "pin").c_str(), inputs[i].pin);
    inputs[i].momentary_mode = preferences.getBool((prefix + "mom").c_str(), inputs[i].momentary_mode);
    inputs[i].name = preferences.getString((prefix + "name").c_str(), inputs[i].name);
    inputs[i].message_on = preferences.getString((prefix + "on").c_str(), inputs[i].message_on);
    inputs[i].message_off = preferences.getString((prefix + "off").c_str(), inputs[i].message_off);
  }

  preferences.end();
  Serial.println(F("Preferences loaded"));
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

  // Save input configurations
  for (int i = 0; i < MAX_INPUTS; i++) {
    String prefix = "in" + String(i) + "_";
    preferences.putBool((prefix + "en").c_str(), inputs[i].enabled);
    preferences.putInt((prefix + "pin").c_str(), inputs[i].pin);
    preferences.putBool((prefix + "mom").c_str(), inputs[i].momentary_mode);
    preferences.putString((prefix + "name").c_str(), inputs[i].name);
    preferences.putString((prefix + "on").c_str(), inputs[i].message_on);
    preferences.putString((prefix + "off").c_str(), inputs[i].message_off);
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
}

void handleRoot() {
  // If in AP mode, show WiFi setup page
  if (apMode) {
    handleWiFiSetup();
    return;
  }

  String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP32-Notifier v");
  html += VERSION;
  html += F("</title><style>"
  "body{font-family:Arial;margin:20px;background:#f0f0f0}.c{max-width:800px;margin:auto;background:#fff;padding:20px;border-radius:8px}"
  "h1{color:#333}.s{margin:15px 0;padding:12px;background:#f9f9f9;border-radius:5px}.s h2{margin-top:0;color:#555;font-size:16px}"
  "label{display:block;margin:8px 0 4px;font-weight:bold;color:#666;font-size:14px}input,textarea,select{width:100%;padding:6px;box-sizing:border-box;border:1px solid #ddd;border-radius:4px;font-size:13px}"
  "input[type=checkbox]{width:auto;margin-right:6px}textarea{min-height:50px;font-family:monospace}"
  "button{background:#4CAF50;color:#fff;padding:8px 16px;border:none;border-radius:4px;cursor:pointer;margin:4px 4px 4px 0;font-size:13px}"
  ".b1{background:#008CBA}.b2{background:#f44336}.b3{background:#FF9800}"
  ".st{padding:8px;background:#e7f3fe;border-left:4px solid #2196F3;margin:10px 0;font-size:13px}"
  ".inp{border:1px solid #ddd;padding:10px;margin:10px 0;background:#fff;border-radius:4px}"
  "</style></head><body><div class='c'><h1>ESP32-Notifier v");
  html += VERSION;
  html += F("</h1><div class='st' id='sb'><b>Status:</b> <span id='st'>Loading...</span></div>");

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

  // Pushbullet
  html += F("<div class='s'><h2>Pushbullet</h2><label><input type='checkbox' name='pb_enabled' value='1'");
  if (pushbullet_enabled) html += F(" checked");
  html += F(">Enable</label><label>Token:</label><input name='pb_token' value='");
  html += htmlEncode(pushbullet_token);
  html += F("'><button type='button' class='b3' onclick='testService(\"pushbullet\")'>Test</button></div>");

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
  html += F("'><button type='button' class='b3' onclick='testService(\"email\")'>Test</button></div>");

  // Telegram
  html += F("<div class='s'><h2>Telegram</h2><label><input type='checkbox' name='tg_enabled' value='1'");
  if (telegram_enabled) html += F(" checked");
  html += F(">Enable</label><label>Bot Token:</label><input name='tg_token' value='");
  html += htmlEncode(telegram_token);
  html += F("'><label>Chat ID:</label><input name='tg_chat' value='");
  html += htmlEncode(telegram_chat_id);
  html += F("'><button type='button' class='b3' onclick='testService(\"telegram\")'>Test</button></div>");

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
    html += F("</textarea></div>");
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

  html += F("<button type='submit'>Save</button><button type='button' class='b1' onclick='location.reload()'>Refresh</button>");
  html += F("<button type='button' class='b3' onclick=\"location.href='/logs'\">View Logs</button>");
  html += F("<button type='button' class='b2' onclick=\"if(confirm('Restart?'))location.href='/restart'\">Restart</button></form></div>");

  html += F("<script>function testService(s){fetch('/test/'+s).then(r=>r.text()).then(d=>alert(s+': '+d))}"
  "fetch('/status').then(r=>r.json()).then(d=>{"
  "let s='WiFi:'+(d.wifi?'OK':'NO')+' |IP:'+d.ip+' |Up:'+Math.floor(d.uptime/1000)+'s';"
  "d.inputs.forEach((inp,i)=>{s+=' |In'+(i+1)+':'+(inp?'H':'L')});"
  "document.getElementById('st').innerHTML=s});"
  "document.querySelector('[name=gmt_offset]').value='");
  html += String(gmt_offset);
  html += F("';</script></body></html>");

  server.send(200, "text/html", html);
}

void handleWiFiSetup() {
  String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>ESP32-Notifier Setup</title><style>"
  "body{font-family:Arial;margin:20px;background:#f0f0f0}.c{max-width:600px;margin:auto;background:#fff;padding:20px;border-radius:8px}"
  "h1{color:#333;text-align:center}.s{margin:15px 0;padding:12px;background:#f9f9f9;border-radius:5px}"
  "label{display:block;margin:8px 0 4px;font-weight:bold;color:#666}input,select{width:100%;padding:8px;box-sizing:border-box;border:1px solid #ddd;border-radius:4px}"
  "button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px;margin-top:10px}"
  "button:hover{background:#45a049}.info{background:#e7f3fe;border-left:4px solid #2196F3;padding:10px;margin:15px 0}"
  ".net{padding:8px;margin:5px 0;background:#fff;border:1px solid #ddd;border-radius:4px;cursor:pointer}"
  ".net:hover{background:#f0f0f0}"
  "</style></head><body><div class='c'><h1>ESP32-Notifier Setup</h1>"
  "<div class='info'>Welcome! Connect to your WiFi network to get started.</div>"
  "<form method='POST' action='/saveWiFi'><div class='s'><h2>WiFi Configuration</h2>"
  "<label>Network Name (SSID):</label><input name='ssid' required placeholder='Your WiFi Network'>"
  "<label>Password:</label><input type='password' name='password' required placeholder='WiFi Password'>"
  "</div><button type='submit'>Connect to WiFi</button></form>"
  "<div class='s' style='margin-top:20px'><small>After connecting, the device will restart and join your WiFi network. "
  "You can then access the full configuration page using the IP address shown in the Serial Monitor.</small></div>"
  "</div></body></html>");

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

  bool timeChanged = false;
  if (server.hasArg("gmt_offset")) {
    long newOffset = server.arg("gmt_offset").toInt();
    if (newOffset != gmt_offset) {
      gmt_offset = newOffset;
      timeChanged = true;
    }
  }
  if (server.hasArg("day_offset")) {
    int newOffset = server.arg("day_offset").toInt();
    if (newOffset != daylight_offset) {
      daylight_offset = newOffset;
      timeChanged = true;
    }
  }

  if (timeChanged) {
    configTime(gmt_offset, daylight_offset, ntpServer);
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

    // Handle enable/disable state changes
    if (!wasEnabled && inputs[i].enabled) {
      pinMode(inputs[i].pin, INPUT_PULLDOWN);
      inputs[i].lastState = digitalRead(inputs[i].pin);
    } else if (wasEnabled && !inputs[i].enabled) {
      pinMode(inputs[i].pin, INPUT);
    }
  }

  savePreferences();
  server.send(200, "text/html", F("<html><head><meta http-equiv='refresh' content='2;url=/'></head><body style='text-align:center;padding:50px'><h1>Saved!</h1></body></html>"));
}

void handleStatus() {
  StaticJsonDocument<512> doc;
  doc["wifi"] = WiFi.status() == WL_CONNECTED;
  doc["ip"] = WiFi.localIP().toString();
  doc["uptime"] = millis();

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

void sendNotifications(String title, String body) {
  Serial.println(F("--- Sending Notifications ---"));

  if (pushbullet_enabled && pushbullet_token.length() > 0) {
    if (!sendPushbulletNotification(title, body)) {
      queueRetry("pushbullet", title, body);
    }
  }

  if (email_enabled && smtp_email.length() > 0 && recipient_email.length() > 0) {
    if (!sendEmailNotification(title, body)) {
      queueRetry("email", title, body);
    }
  }

  if (telegram_enabled && telegram_token.length() > 0 && telegram_chat_id.length() > 0) {
    if (!sendTelegramNotification(body)) {
      queueRetry("telegram", title, body);
    }
  }

  Serial.println(F("--- Notifications Complete ---"));
}

void handleLogs() {
  String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
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

bool sendPushbulletNotification(String title, String body) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("✗ Pushbullet: WiFi down"));
    return false;
  }

  HTTPClient http;
  http.begin("https://api.pushbullet.com/v2/pushes");
  http.addHeader("Access-Token", pushbullet_token);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["type"] = "note";
  doc["title"] = title;
  doc["body"] = body;

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

bool sendEmailNotification(String subject, String body) {
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

  if (!smtp.connect(&session)) {
    Serial.println(F("✗ Email: SMTP connect failed"));
    return false;
  }

  bool success = MailClient.sendMail(&smtp, &message);
  smtp.closeSession();

  if (success) {
    Serial.println(F("✓ Email sent"));
    addLog("SUCCESS", "Email notification sent to " + recipient_email);
    return true;
  } else {
    Serial.print(F("✗ Email failed: "));
    Serial.println(smtp.errorReason().c_str());
    addLog("ERROR", "Email failed - " + String(smtp.errorReason().c_str()));
    return false;
  }
}

bool sendTelegramNotification(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("✗ Telegram: WiFi down"));
    return false;
  }

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + telegram_token + "/sendMessage";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
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
