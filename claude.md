# Project Context for Claude

## Project Overview

**Name: ESP32-Notifier**
**Version: 2.0**

This is an ESP32-S3 based IoT notification system that monitors **up to 4 physical switches** simultaneously and sends real-time notifications via multiple channels (Pushbullet, Email, Telegram) when device states change. Features a password-protected web-based configuration interface with persistent storage, WiFi auto-reconnection, watchdog timer, notification retry logic, and comprehensive security improvements.

## Major Changes in v2.0

### WiFi Setup Mode (NEW!)
- Automatic Access Point mode when WiFi credentials are empty
- AP SSID: "ESP32-Notifier-Setup" with password "setup123"
- Simple web-based WiFi configuration (no code editing required)
- Automatic restart and connection after WiFi setup
- Default AP IP: 192.168.4.1

### Security Enhancements
- HTTP Basic Authentication on all web endpoints (except AP setup)
- HTML entity encoding to prevent XSS
- ArduinoJson library for safe JSON handling (no injection vulnerabilities)
- Configurable username/password stored in preferences

### Reliability Improvements
- 30-second hardware watchdog timer prevents system hangs
- Non-blocking WiFi connection (no blocking delays in setup)
- Automatic WiFi reconnection every 30 seconds if connection drops
- Notification retry queue with exponential backoff (3 retries max)
- Rate limiting (5 second minimum between notifications per input)
- Proper SMTP session cleanup

### Multi-Input Support
- Support for up to 4 independent inputs (configurable MAX_INPUTS)
- Each input has:
  - Individual enable/disable toggle
  - Custom name
  - Configurable GPIO pin
  - Independent toggle/momentary mode
  - Custom ON/OFF messages
  - Independent debounce and rate limiting state

### User Experience Improvements
- Timezone dropdown with all major timezones (UTC-12 to UTC+14)
- Test buttons for each notification service
- Live status display showing all input states
- System logging with web-based log viewer (last 100 events)
- Logs auto-refresh every 10 seconds
- Color-coded log levels (INFO, SUCCESS, WARNING, ERROR)

### Code Quality
- Preference keys in namespace to prevent typos
- Notification functions return bool for success/failure tracking
- F() macro for constant strings (saves RAM)
- Input sanitization and validation
- Dynamic pin reconfiguration without restart
- Timezone changes apply immediately

## Hardware Platform

- **Microcontroller**: ESP32-S3 (chosen over ATtiny85 for WiFi capability and sufficient flash memory)
- **Switch Input**: GPIO4 with internal pull-down resistor
- **Communication**: WiFi for internet connectivity
- **Flash Requirements**: Requires "Huge APP (3MB No OTA/1MB SPIFFS)" partition scheme

## Software Architecture

### Core Components

1. **WiFi Connection**
   - Default SSID: (empty - must configure)
   - Default Password: (empty - must configure)
   - Configurable via web interface
   - Connection timeout with fallback

2. **Web Server**
   - HTTP server on port 80
   - Minified HTML for flash space optimization
   - Routes: / (config), /save (POST), /status (JSON), /restart
   - Real-time status updates via AJAX

3. **Time Synchronization**
   - NTP server: pool.ntp.org
   - Configurable timezone offset (GMT seconds)
   - Configurable daylight saving offset
   - Provides timestamps for notifications

4. **Switch Monitoring**
   - GPIO: Pin 4 (INPUT_PULLDOWN mode, configurable)
   - Debounce delay: 50ms
   - Two modes: Toggle and Momentary
   - State change detection with Serial diagnostics

5. **Notification Services**

   **Pushbullet:**
   - API endpoint: https://api.pushbullet.com/v2/pushes
   - Default: Disabled (token must be configured)
   - JSON payload format
   - Enable/disable toggle

   **Email (SMTP):**
   - Uses ESP_Mail_Client library
   - Supports Gmail, Outlook, Yahoo, custom SMTP
   - Configurable server, port, credentials
   - SSL/TLS support (ports 465/587)
   - Enable/disable toggle

   **Telegram:**
   - Bot API via HTTPS
   - Configurable bot token and chat ID
   - JSON payload format
   - Enable/disable toggle

6. **Persistent Storage**
   - Uses ESP32 Preferences library
   - Namespace: "notifier"
   - Stores all configuration in NVS (Non-Volatile Storage)
   - Survives power cycles and reboots

### Notification Logic

**Toggle Mode (default):**
- Sends notifications on both HIGH and LOW transitions
- Use cases: Door sensors, window sensors, power monitors

**Momentary Mode:**
- Sends notification only on HIGH transition (ignores LOW)
- Use cases: Doorbells, motion detectors, push buttons

Message format:
- ON: Customizable with {timestamp} placeholder
- OFF: Customizable with {timestamp} placeholder
- Default: "Device turned ON/OFF at [timestamp]"
- Timestamp format: YYYY-MM-DD HH:MM:SS

### Parallel Notifications
- All enabled services receive notifications simultaneously
- Individual enable/disable toggles for each service
- Detailed Serial Monitor feedback with ✓/✗ indicators

## Dependencies

- WiFi.h (ESP32 core library)
- WebServer.h (ESP32 core library)
- HTTPClient.h (ESP32 core library)
- Preferences.h (ESP32 core library)
- time.h (standard C library)
- esp_task_wdt.h (ESP32 core library - watchdog)
- ESP_Mail_Client.h (by Mobizt - external library, v3.x+)
- ArduinoJson.h (by Benoit Blanchon - external library, v6.x+)

## Configuration Parameters

### Default Values

| Parameter | Default Value | Storage Key | Purpose |
|-----------|---------------|-------------|---------|
| VERSION | "2.0" | N/A | Software version |
| MAX_INPUTS | 4 | N/A | Maximum number of inputs |
| WDT_TIMEOUT | 30 | N/A | Watchdog timeout (seconds) |
| wifi_ssid | "" | wifi_ssid | WiFi network name |
| wifi_password | "" | wifi_pass | WiFi password |
| web_username | "admin" | web_user | Web auth username |
| web_password | "admin123" | web_pass | Web auth password |
| pushbullet_token | "" | pb_token | Pushbullet API token |
| pushbullet_enabled | false | pb_enabled | Enable Pushbullet |
| smtp_host | "smtp.gmail.com" | smtp_host | SMTP server |
| smtp_port | 465 | smtp_port | SMTP port |
| smtp_email | "" | smtp_email | Sender email |
| smtp_password | "" | smtp_pass | Email password |
| recipient_email | "" | rcpt_email | Notification recipient |
| email_enabled | false | email_enabled | Enable email |
| telegram_token | "" | tg_token | Telegram bot token |
| telegram_chat_id | "" | tg_chat | Telegram chat ID |
| telegram_enabled | false | tg_enabled | Enable Telegram |
| notification_title | "Device Status Alert" | notif_title | Notification title |
| gmt_offset | 0 | gmt_offset | Timezone offset (seconds) |
| daylight_offset | 0 | day_offset | DST offset (seconds) |
| DEBOUNCE_DELAY | 50 | N/A | Debounce delay (ms) |
| MIN_NOTIFICATION_INTERVAL | 5000 | N/A | Rate limit per input (ms) |
| MAX_RETRIES | 3 | N/A | Notification retry attempts |
| RETRY_DELAY | 60000 | N/A | Base retry delay (ms) |
| WIFI_TIMEOUT | 20000 | N/A | WiFi connection timeout |
| WIFI_CHECK_INTERVAL | 30000 | N/A | WiFi reconnect check interval |
| TIME_SYNC_RETRY | 3600000 | N/A | NTP retry interval (1 hour) |

### Input Configuration (per input, i=0-3)

| Parameter | Default (Input 1) | Storage Key | Purpose |
|-----------|-------------------|-------------|---------|
| inputs[i].enabled | true (i=0), false (others) | in{i}_en | Enable this input |
| inputs[i].pin | 4, 5, 6, 7 | in{i}_pin | GPIO pin number |
| inputs[i].momentary_mode | false | in{i}_mom | Toggle or momentary mode |
| inputs[i].name | "Input {i+1}" | in{i}_name | Display name |
| inputs[i].message_on | "Input {i+1} ON..." | in{i}_on | ON message template |
| inputs[i].message_off | "Input {i+1} OFF..." | in{i}_off | OFF message template |

## Code Structure

### Main Files
- `esp32_notifier/esp32_notifier.ino` - Main sketch (minified HTML for space)

### Key Functions

**Initialization:**
- `setup()` - Initialize watchdog, hardware, WiFi, web server
- `loadPreferences()` - Load all config from NVS including multi-inputs
- `savePreferences()` - Save all config to NVS including multi-inputs

**Main Loop:**
- `loop()` - Feed watchdog, update WiFi, handle web, retry queue, monitor all inputs
- `monitorInput(int index)` - Monitor single input with debounce and rate limiting
- `updateWiFiConnection()` - Non-blocking WiFi state machine
- `checkAndReconnectWiFi()` - Periodic WiFi health check and reconnect
- `attemptTimeSync()` - Periodic NTP sync retry

**Web Server:**
- `setupWebServer()` - Configure all routes with authentication
- `handleRoot()` - Serve config page with multi-input UI (or WiFi setup in AP mode)
- `handleWiFiSetup()` - Serve WiFi configuration page in AP mode
- `handleSaveWiFi()` - Save WiFi credentials and restart
- `handleSave()` - Process form, validate, update pins dynamically
- `handleStatus()` - Return JSON with all input states
- `handleLogs()` - Serve log viewer page with last 100 entries
- `handleRestart()` - Reboot device
- `handleTestPushbullet()` - Test Pushbullet service
- `handleTestEmail()` - Test email service
- `handleTestTelegram()` - Test Telegram service
- `htmlEncode(String)` - Sanitize output for XSS prevention

**Notifications:**
- `sendNotifications(title, body)` - Dispatch to all enabled services, queue failures
- `sendPushbulletNotification(title, body)` - Return bool for success
- `sendEmailNotification(subject, body)` - Return bool, close session
- `sendTelegramNotification(message)` - Return bool for success
- `queueRetry(service, title, body)` - Add failed notification to retry queue
- `processRetryQueue()` - Process retry queue with exponential backoff

**WiFi Management:**
- `startAccessPoint()` - Start AP mode for initial WiFi setup
- `connectWiFiNonBlocking()` - Start non-blocking WiFi connection in station mode

**Logging:**
- `addLog(level, message)` - Add entry to circular log buffer and print to Serial
- Log levels: INFO, SUCCESS, WARNING, ERROR
- Circular buffer stores last 100 entries

**Utilities:**
- `getFormattedTime()` - Get NTP timestamp string

## Flash Memory Optimization

Due to the extensive feature set (3 notification services + web interface), the sketch requires:
- **Partition Scheme**: "Huge APP (3MB No OTA/1MB SPIFFS)"
- **HTML Minification**: Removed whitespace, shortened CSS classes
- **String Concatenation**: Used for HTML instead of raw literals

Without the correct partition scheme, compilation fails with "Sketch too big" error.

## User Requirements (v2.0 - ALL IMPLEMENTED)

✅ Multiple notification channels (Pushbullet, Email, Telegram)
✅ Alert when status changes (both ON and OFF)
✅ Include date/time in notifications
✅ Use ESP32-S3 hardware
✅ **Multi-input support (4 switches)**
✅ **WiFi Setup Mode (AP mode for first-time setup)**
✅ Web configuration portal with authentication
✅ Persistent settings storage
✅ Momentary switch support
✅ Customizable messages per input
✅ Individual service toggles
✅ Serial diagnostics
✅ **WiFi auto-reconnection**
✅ **Watchdog timer**
✅ **Notification retry logic**
✅ **Rate limiting**
✅ **Test buttons for services**
✅ **Security (auth, XSS, JSON injection protection)**
✅ **Non-blocking operations**

## Completed Features (v2.0)

✅ WiFi Setup Mode with Access Point (ESP32-Notifier-Setup)
✅ System logging with circular buffer (100 entries)
✅ Web-based log viewer with auto-refresh
✅ Color-coded log levels (INFO, SUCCESS, WARNING, ERROR)
✅ Multi-input support (up to 4 switches)
✅ WiFi auto-reconnection with monitoring
✅ Hardware watchdog timer (30s)
✅ Web authentication (HTTP Basic Auth)
✅ Non-blocking WiFi connection
✅ Notification retry queue with exponential backoff
✅ Rate limiting per input (5s minimum)
✅ Test buttons for all notification services
✅ Timezone dropdown selector (UTC-12 to UTC+14)
✅ ArduinoJson for secure JSON handling
✅ HTML encoding for XSS prevention
✅ Dynamic GPIO pin reconfiguration
✅ Immediate timezone update
✅ SMTP session cleanup
✅ Preference key namespace
✅ Status endpoint with all input states
✅ F() macro for flash string storage

## Remaining Enhancement Ideas

- OTA (Over-The-Air) firmware updates
- Battery level monitoring
- MQTT support
- Deep sleep mode for power saving
- WiFi network scanner in AP setup mode
- Persistent log storage (save to SPIFFS/LittleFS)
- Log export (download as CSV/JSON)
- Webhook support (IFTTT, Home Assistant)
- SMS via Twilio
- Configurable debounce delay per input
- Scheduled notifications
- Email multiple recipients (comma-separated)
- Discord webhook support
