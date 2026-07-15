# Tally Light 2.0

An ESP32 firmware project for broadcast tally lights — hardware indicators that show camera operators and talent when a camera is live, in preview, or idle.

## Project overview

- **Platform:** ESP32 (espressif32), Arduino framework via PlatformIO
- **Build tool:** PlatformIO (`platformio.ini`)
- **Four build environments:** `tally_camera_0` through `tally_camera_3` — each flashes firmware for a different camera ID (`CAM_ID` define)

## Source layout

| Path | Purpose |
|------|---------|
| `src/main.cpp` | Entry point — reads a hardware switch on GPIO 4 to select operating mode, then delegates to one of the two mode modules |
| `src/MODE_TALLYHUB_32.cpp/.h` | TallyHub mode (network protocol for tally hubs) |
| `src/MODE_UDPWS_32.cpp/.h` | UDP/WebSocket mode |
| `src/config.h` | WiFi SSID and password (edit before flashing) |
| `platformio.ini` | PlatformIO build environments and library dependencies |

## Libraries used

- **Adafruit NeoPixel** — controls the RGB LED strip/pixels that show tally status
- **ArduinoJson** — parses JSON messages from the tally hub or UDP source
- **WiFiManager** — captive-portal WiFi configuration so devices can be provisioned without recompiling

## How to build (requires PlatformIO)

This project targets physical ESP32 hardware and cannot be run on Replit directly. To compile and flash:

```bash
pio run -e tally_camera_0        # build for camera 0
pio run -e tally_camera_0 -t upload  # build and flash
```

## User preferences

<!-- Add any personal notes or conventions here -->
