# ESP32-Notifier v2.0 Release Notes

## 🎉 Major Release - October 2025

This is a comprehensive rewrite of ESP32-Notifier with significant new features, security improvements, and enhanced reliability.

## ✨ New Features

### WiFi Setup Mode
- **Access Point Mode** - Automatic AP creation when WiFi credentials are empty
- **Web-based WiFi Configuration** - No code editing required for first-time setup
- **SSID**: `ESP32-Notifier-Setup` (Password: `setup123`)
- **Setup URL**: `http://192.168.4.1`

### System Logging
- **Circular Log Buffer** - Stores last 100 system events
- **Web-based Log Viewer** - Dark-themed interface with auto-refresh
- **Color-coded Log Levels** - INFO (blue), SUCCESS (green), WARNING (orange), ERROR (red)
- **Comprehensive Logging** - Tracks triggers, notifications, errors, WiFi events

### Multi-Input Support
- **Up to 4 Independent Inputs** - Monitor multiple switches/sensors simultaneously
- **Individual Configuration** - Each input has its own name, GPIO pin, mode, and messages
- **Enable/Disable per Input** - Flexible control over which inputs are active
- **Independent Rate Limiting** - Prevents spam on each input separately

### User Experience Improvements
- **Timezone Dropdown** - Easy selection from UTC-12 to UTC+14 (all major timezones)
- **Test Buttons** - Test each notification service directly from web interface
- **Live Status Display** - Real-time WiFi, IP, uptime, and all input states
- **Improved Web UI** - Cleaner design with better organization

## 🔒 Security Enhancements

- **HTTP Basic Authentication** - Password-protected web interface (default: admin/admin123)
- **XSS Prevention** - HTML entity encoding for all user-supplied content
- **JSON Injection Protection** - ArduinoJson library for safe JSON handling
- **Input Validation** - Sanitized inputs prevent injection attacks

## 🛡️ Reliability Improvements

### WiFi & Connectivity
- **Auto-Reconnection** - Automatic WiFi recovery every 30 seconds
- **Non-blocking Operations** - No blocking delays during WiFi connection
- **Connection Monitoring** - Periodic health checks

### System Stability
- **Hardware Watchdog Timer** - 30-second timeout prevents system hangs
- **Notification Retry Queue** - Failed notifications auto-retry up to 3 times
- **Exponential Backoff** - Smart retry delays (60s, 120s, 240s)
- **Rate Limiting** - 5-second minimum interval between notifications per input
- **Proper SMTP Cleanup** - Prevents email session leaks

### Code Quality
- **Preference Key Namespace** - Prevents configuration typos
- **Boolean Return Values** - All notification functions return success/failure
- **F() Macro** - Stores constant strings in flash to save RAM
- **Dynamic Pin Reconfiguration** - Change GPIO pins without restart
- **Immediate Timezone Updates** - No restart needed for timezone changes

## 📝 Technical Details

### Dependencies
- **ESP32 Arduino Core** - v3.x+ (updated watchdog API)
- **ESP Mail Client** - by Mobizt (SMTP support)
- **ArduinoJson** - v6.x+ (secure JSON handling)

### Memory Usage
- **Program Storage**: 44% of available space
- **Dynamic Memory**: 14% of available RAM
- **Partition Scheme Required**: "Huge APP (3MB No OTA/1MB SPIFFS)"

### Configuration Storage
All settings persist in NVS (Non-Volatile Storage):
- WiFi credentials
- Web authentication
- Notification service credentials
- Input configurations (4 inputs)
- Timezone settings
- Custom messages

## 🔧 Breaking Changes

### Code Changes
- Watchdog timer API updated for ESP32 Arduino Core 3.x
- Directory renamed from `switch_notifier` to `esp32_notifier`
- Default WiFi credentials removed (must configure via AP mode or code)
- Pushbullet disabled by default (enable via web interface)

### Migration from v1.0
If upgrading from v1.0:
1. Back up your WiFi credentials and notification tokens
2. Upload new v2.0 code
3. Device will start in AP mode if credentials not found
4. Configure via web interface at `http://192.168.4.1`
5. Re-enter all notification service credentials
6. Configure additional inputs if needed

## 📊 Statistics

- **Lines of Code**: ~1,050 lines in main sketch
- **Log Buffer Size**: 100 entries (circular buffer)
- **Max Inputs**: 4 (configurable via MAX_INPUTS constant)
- **Notification Services**: 3 (Pushbullet, Email, Telegram)
- **Max Retry Attempts**: 3 per failed notification
- **Debounce Delay**: 50ms
- **Rate Limit**: 5 seconds per input
- **WiFi Timeout**: 20 seconds
- **WiFi Check Interval**: 30 seconds
- **Watchdog Timeout**: 30 seconds

## 🐛 Known Issues

None currently identified. Please report issues at: https://github.com/cyberpods/esp32-notifier/issues

## 🔮 Future Enhancements

Potential features for future releases:
- OTA (Over-The-Air) firmware updates
- MQTT support
- Persistent log storage (SPIFFS/LittleFS)
- Log export (CSV/JSON download)
- WiFi network scanner in AP mode
- Battery level monitoring
- Deep sleep mode for power saving
- Webhook support (IFTTT, Home Assistant)
- SMS notifications via Twilio
- Configurable debounce delay per input

## 📚 Documentation

- [README.md](README.md) - Complete setup and usage guide
- [Wiring Diagrams](docs/) - Visual guides for hardware connections
- [Web Interface Screenshots](docs/) - Preview of configuration pages
- [Terminal Output Example](docs/terminal-output.txt) - Sample serial monitor output
- [Sample Log File](docs/sample-log.json) - JSON format example

## 🙏 Acknowledgments

Built with:
- ESP32 Arduino Core by Espressif Systems
- ESP Mail Client by Mobizt
- ArduinoJson by Benoit Blanchon
- Claude Code for development assistance

## 📄 License

MIT License - See [LICENSE](LICENSE) file for details

---

**Download**: [esp32-notifier v2.0](https://github.com/cyberpods/esp32-notifier/releases/tag/v2.0)

**Repository**: https://github.com/cyberpods/esp32-notifier

**Issues/Support**: https://github.com/cyberpods/esp32-notifier/issues
