# BMP180 Sensor Integration Guide

## Overview

The ESP32-Notifier now supports the BMP180 (and BMP085) barometric pressure and temperature sensor. This feature was added in version 3.2.

## Features

- **Real-time environmental monitoring**:
  - Temperature (°C and °F)
  - Barometric pressure (hPa and inHg)
  - Altitude estimation (meters and feet)

- **Automatic inclusion in notifications**: Sensor data is automatically appended to all notifications when enabled

- **Live web interface**: View current readings in real-time from the configuration web page

- **Configurable I2C pins**: Customize SDA and SCL pins to avoid conflicts with other peripherals

## Hardware Requirements

### BMP180 Sensor Module
- Operating voltage: 3.3V (5V tolerant on some modules)
- Communication: I2C interface
- I2C Address: 0x77 (fixed)

### Wiring Diagram

```
BMP180 Module      ESP32-S3
─────────────      ────────
VCC (3.3V)    ->   3.3V
GND           ->   GND
SDA           ->   GPIO21 (default, configurable)
SCL           ->   GPIO22 (default, configurable)
```

**Note**: The default I2C pins are GPIO21 (SDA) and GPIO22 (SCL). These can be changed via the web interface.

### Pin Recommendations by Board Type

**Generic ESP32-S3:**
- Default: SDA=21, SCL=22
- Alternative: SDA=33, SCL=34

**ESP32-S3-SIM7670G-4G (Waveshare):**
- Recommended: SDA=21, SCL=47
- Avoid pins used by modem (17, 18, 41, 42) and camera (4-16, 34-37)

**Freenove ESP32-S3:**
- Recommended: SDA=21, SCL=22
- Avoid camera I2C pins (4, 5)

## Software Setup

### 1. Install Required Library

In Arduino IDE:
1. Go to **Tools → Manage Libraries**
2. Search for **"Adafruit BMP085 Library"** (works with both BMP085 and BMP180)
3. Click **Install**
4. Also install dependencies: **"Adafruit Unified Sensor"** and **"Adafruit BusIO"**

### 2. Upload Firmware

1. Open `esp32_notifier.ino` in Arduino IDE
2. Ensure `#define HAS_BMP180_LIB` is uncommented (enabled by default)
3. Select your board and upload

### 3. Web Configuration

1. Access the device's web interface (e.g., `http://192.168.1.100`)
2. Log in with your credentials
3. Scroll to the **"BMP180 Sensor (Temperature/Pressure)"** section
4. Configure the following:
   - ✅ **Enable BMP180 Sensor**: Check to enable
   - **SDA Pin**: GPIO pin for I2C data (default: 21)
   - **SCL Pin**: GPIO pin for I2C clock (default: 22)
   - ✅ **Include sensor data in notifications**: Check to append readings to all notifications
5. Click **"Save Configuration"**
6. The sensor will initialize automatically

### 4. Verify Operation

After saving:
- The web interface will show sensor status:
  - ✅ **"Sensor: Initialized"** if working correctly
  - ⚠️ **"Sensor not detected - check wiring"** if there's a connection issue
- Live readings will appear:
  ```
  🌡️ Temperature: 22.5°C / 72.5°F
  🔽 Pressure: 1013.2 hPa / 29.92 inHg
  ⛰️ Altitude: 123.4 m / 404.9 ft
  ```

## Notification Format

When enabled, sensor data is automatically appended to all notifications:

### Example Notification
```
Input 1 ON at 2025-10-15 14:30:45

🌡️  Temperature: 22.5°C / 72.5°F
🔽 Pressure: 1013.2 hPa / 29.92 inHg
⛰️  Altitude: 123.4 m / 404.9 ft
```

The sensor data includes:
- Temperature in both Celsius and Fahrenheit
- Pressure in both hectopascals (hPa/mbar) and inches of mercury (inHg)
- Estimated altitude in both meters and feet

## Troubleshooting

### Sensor Not Detected

**Problem**: Web interface shows "⚠️ Sensor not detected - check wiring"

**Solutions**:
1. **Check wiring**:
   - Verify VCC is connected to 3.3V (NOT 5V on most ESP32 boards)
   - Confirm GND is connected
   - Ensure SDA and SCL are connected to the correct GPIO pins

2. **Verify I2C address**:
   - BMP180 uses I2C address 0x77
   - Run an I2C scanner sketch to confirm the sensor is detected

3. **Check GPIO pins**:
   - Ensure pins aren't already in use by other peripherals
   - Try different GPIO pins if there's a conflict
   - Update pin configuration in web interface

4. **Test with minimal setup**:
   - Disconnect other I2C devices
   - Use shorter wires (< 30cm recommended)
   - Add pull-up resistors (4.7kΩ) on SDA and SCL if using long wires

### Incorrect Readings

**Problem**: Sensor readings seem wrong

**Solutions**:
1. **Temperature too high**: Ensure sensor isn't near heat sources (voltage regulators, ESP32 chip)
2. **Pressure inconsistent**: Allow 1-2 minutes for sensor to stabilize after power-on
3. **Altitude offset**: Altitude is calculated from pressure using standard atmospheric pressure. Readings are relative, not absolute.

### Serial Monitor Debugging

Enable Serial Monitor (115200 baud) to see detailed initialization:
```
[BOOT] Initializing BMP180 sensor...
I2C pins - SDA: GPIO21, SCL: GPIO22
✓ BMP180 sensor initialized successfully
```

If initialization fails:
```
ERROR: Could not find BMP180 sensor!
Check wiring:
  VCC -> 3.3V
  GND -> GND
  SDA -> GPIO21
  SCL -> GPIO22
```

## Technical Details

### Sensor Specifications
- **Temperature range**: -40°C to +85°C
- **Temperature accuracy**: ±2°C
- **Pressure range**: 300 to 1100 hPa
- **Pressure accuracy**: ±1 hPa
- **Response time**: < 5ms
- **Power consumption**: 5µA (low power mode)

### Altitude Calculation
Altitude is calculated using the barometric formula with standard sea-level pressure (1013.25 hPa). For accurate altitude readings:
- Calibrate using known altitude reference
- Compensate for weather-related pressure changes
- Note: Altitude is estimated and should not be used for critical applications

### Sensor Polling
- The sensor is read continuously in the main loop
- Readings are cached and used when notifications are sent
- Update frequency: ~10-20 times per second (fast enough for environmental changes)

## Integration with Other Features

### GPS + BMP180
When both GPS and BMP180 are enabled:
```
Input 1 triggered at 2025-10-15 14:30:45

GPS: Lat: 40.7128, Lon: -74.0060, Alt: 10.0m (4 sats)

🌡️  Temperature: 22.5°C / 72.5°F
🔽 Pressure: 1013.2 hPa / 29.92 inHg
⛰️  Altitude: 123.4 m / 404.9 ft
```

### Camera + BMP180
Photos captured with triggers will include environmental data in the notification text.

## Use Cases

1. **Weather Station**: Monitor temperature and pressure trends
2. **Greenhouse Monitoring**: Track environmental conditions for plants
3. **Server Room**: Alert when temperature or pressure changes significantly
4. **Outdoor Projects**: Monitor altitude changes for hiking/climbing applications
5. **HVAC Monitoring**: Detect pressure changes in ventilation systems

## API Access

The sensor provides these functions (for advanced users):
```cpp
bool initBMP180()                  // Initialize sensor
void updateBMP180()                // Read current values
String getBMP180Data()             // Get compact data string
String getBMP180DataFormatted()    // Get formatted data with emojis
```

## Disabling BMP180 Support

To remove BMP180 support (save memory):
1. Comment out `#define HAS_BMP180_LIB` in the code
2. Uncheck "Enable BMP180 Sensor" in web interface
3. Re-upload firmware

## Support

For issues or questions:
- GitHub: https://github.com/anthropics/claude-code/issues
- Check existing issues for BMP180-related problems
- Include Serial Monitor output when reporting bugs

## License

MIT License - Same as ESP32-Notifier project
