# LOHO Squeeze

A robust IoT light control solution built on an ESP32-C3, featuring MQTT connectivity and a web interface with custom styling.

## Features

### Connectivity & Discovery
- **SSDP Discovery**: Announces device presence via SSDP multicast (224.0.0.251:1900) after WiFi connects and MDNS is initialized; unique lamp ID derived from ESP32-C3 efuse MAC address.
- **MQTT Integration**: Publishes state to MQTT topics and supports remote on/off/brightness commands; includes Home Assistant discovery payloads.

### Web Interface
- Serves a styled dashboard from `/data` directory (`index.html`, `settings.html`, `style.css`) via embedded HTTP server using LittleFS for persistent storage.
- Includes fallback AP mode with background reconnection logic to restore normal Wi-Fi when available.

### Light Control
- PWM-based dimming with gamma correction for perceptually linear brightness control.
- Physical button interface with debouncing, long-press detection, and radio toggle functionality.

## Getting Started

This project uses PlatformIO for build and deployment.

### Prerequisites
- [PlatformIO Core](https://platformio.org/download) installed on your system.
- ESP32-C3 development board.

### Setup & Build
1. Clone the repository: `git clone <repo_url>`
2. Open in VS Code with PlatformIO extension.
3. Configure your environment in `platformio.ini` (see below).
4. Build and upload using the PlatformIO sidebar: **Build** → **Upload**.

### Flashing

The firmware alone is not enough - the web interface is served from a LittleFS
image built out of `data/`, and that image is **not** written by a normal
Upload. A complete flash is two steps:

```bash
pio run -t upload      # firmware
pio run -t uploadfs    # data/ -> LittleFS partition
```

Skip `uploadfs` and the device boots fine but every page returns
"Filesystem not mounted".

If you change `partitions.csv`, erase the chip first. NVS lives at a fixed
offset, so a re-layout leaves stale settings (Wi-Fi credentials) that decode as garbage:

```bash
pio run -t erase
```

### Troubleshooting: web UI unreachable

Symptom: the device joins Wi-Fi and gets an IP, but the web page will not load,
and the serial monitor repeats:

```
fail on fd <N>, errno: 11, "No more processes"
```

`errno 11` is `EAGAIN`. The ESP32-C3 has a **single radio** shared between Wi-Fi and BLE.
When both stacks are active, coexistence arbitration can cause Wi-Fi reads to miss their
deadline. This project only runs the web server stack, so this shouldn't happen:
but if you ever see it, try disabling any BLE advertising or rebooting.

Confirm on serial that the device is in STA mode:

```
[STACK] Starting Web Server stack...
```

### Flash layout

4MB flash, factory-only (there is no OTA code in this firmware, so the
`otadata`/`ota_0`/`ota_1` trio would only waste space):

| Partition | Offset | Size |
| --- | --- | --- |
| bootloader | `0x0` | 32 KB |
| partition table | `0x8000` | 4 KB |
| `nvs` | `0x9000` | 20 KB |
| `app` (factory) | `0x10000` | 3456 KB |
| `spiffs` (LittleFS) | `0x370000` | 576 KB |

The binary is a large one, so most of the flash goes to the app. If a build ever fails to boot with `Image length ... doesn't fit in partition length` / `No bootable app partition`, the app outgrew its partition - that message comes from the bootloader, not from the upload.

The `spiffs` partition keeps that name and SubType deliberately even though it is formatted as LittleFS: both `LittleFS.begin()` and `uploadfs` locate the partition by subtype, and naming it `littlefs` trips a warning in `gen_esp32part.py`.

## Configuration (`platformio.ini`)

The project is configured for the `esp32c3` environment with required libraries defined in the `.pio` configuration file.

## Architecture Overview

- `src/main.cpp`: Entry point initializing all subsystems and starting the main loop.
- `src/discovery.*`: SSDP presence broadcasting and unique lamp ID generation from efuse MAC.
- `src/light_control.*`: PWM dimming, gamma correction, button debouncing, and radio toggle logic.
- `src/mqtt_handler.*`: MQTT client setup, state publishing, and Home Assistant discovery.
- **Removed**: Matter protocol implementation (no longer included).
- `src/web_server.*`: Embedded HTTP server with LittleFS storage and fallback AP mode handling.
- `include/`: Shared headers for project-wide constants and declarations.

## Library Dependencies

The project uses the following PlatformIO libraries (defined in `platformio.ini`):

- **ESPmDNS** – mDNS/SSDP discovery support.
- **ArduinoJson** – JSON serialization for SSDP requests and MQTT payloads.
- **PubSubClient** – MQTT client library.
- **WebServer**, **DNSServer**, **LittleFS** – HTTP server, DNS handling, and persistent file storage.
