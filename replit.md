# Tally Light 2.0

A firmware project for broadcast tally lights — hardware indicators that show camera operators and talent when a camera is live, in preview, or idle. Supports both ESP32 and ESP8266 microcontrollers.

## Project overview

- **Platform:** ESP32 (espressif32) and ESP8266 (espressif8266), Arduino framework via PlatformIO
- **Build tool:** PlatformIO (`platformio.ini`)
- **Build environments:**
  - ESP32: `tally_esp32_cam0` through `tally_esp32_cam3`
  - ESP8266: `tally_esp8266_cam0` through `tally_esp8266_cam3`
- Each environment flashes firmware for a different camera ID via the `CAM_ID` build flag

## Source layout

| Path | Purpose |
|------|---------|
| `src/main.cpp` | Entry point — reads a hardware switch on GPIO 4 to select operating mode, then delegates to one of the two mode modules |
| `src/MODE_TALLYHUB.cpp/.h` | TallyHub mode (network protocol for tally hubs) |
| `src/MODE_UDPWS.cpp/.h` | UDP/WebSocket mode |
| `src/config.h` | WiFi SSID and password for UDP mode (edit before flashing) |
| `platformio.ini` | PlatformIO build environments and library dependencies |

## Libraries used

- **Adafruit NeoPixel** — controls the RGB LED strip/pixels that show tally status
- **ArduinoJson** — parses JSON messages from the tally hub or UDP source
- **WiFiManager** — captive-portal WiFi configuration so devices can be provisioned without recompiling

## How to build (requires PlatformIO)

This project targets physical hardware and cannot be run on Replit directly. To compile and flash:

```bash
# ESP32
pio run -e tally_esp32_cam0
pio run -e tally_esp32_cam0 -t upload

# ESP8266
pio run -e tally_esp8266_cam0
pio run -e tally_esp8266_cam0 -t upload
```

## User preferences

<!-- Add any personal notes or conventions here -->
