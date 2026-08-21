# LOHO Squeeze

A robust IoT light control solution built on an ESP32-C3, featuring multi-protocol connectivity (Matter & MQTT), a web interface with custom styling, and modular handler components for easy extension.

## Features

### Connectivity & Discovery
- **SSDP Discovery**: Announces device presence via SSDP multicast (224.0.0.251:1900) after WiFi connects and MDNS is initialized; unique lamp ID derived from ESP32-C3 efuse MAC address.
- **Matter Protocol**: Full Matter dimmable light integration for smart home control (HomeKit, Google Home, Home Assistant).
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
offset, so a re-layout leaves stale settings (Wi-Fi credentials, Matter
commissioning) that decode as garbage:

```bash
pio run -t erase
```

### Troubleshooting: web UI unreachable when Matter is enabled

Symptom: the device joins Wi-Fi and gets an IP, but the web page will not load,
and the serial monitor repeats:

```
fail on fd <N>, errno: 11, "No more processes"
```

`errno 11` is `EAGAIN`. The "No more processes" text is newlib's legacy string
for that code - it has nothing to do with processes or with running out of any
resource, and it is a red herring.

Cause: the ESP32-C3 has a **single radio** shared between Wi-Fi and BLE. While
Matter is uncommissioned it advertises over BLE continuously, and coexistence
arbitration hands a large share of airtime to BLE. Wi-Fi reads then miss their
deadline and return EAGAIN - and `NetworkClient::available()` in the Arduino
core calls `stop()` on that without retrying (unlike `write()`, which
explicitly tolerates EAGAIN), so the HTTP connection is torn down mid-request.

This does not appear in AP mode, because Matter only starts once
`WiFi.status() == WL_CONNECTED`.

Fix: **this project does not use BLE at all.** `setupMatter()` only runs once
`WiFi.status() == WL_CONNECTED`, so the device always already has IP
connectivity and can be commissioned *on-network*. After `Matter.begin()`,
`matter_handler.cpp` closes the BLE-advertising window that the stack opens by
default and reopens it with `CommissioningWindowAdvertisement::kDnssdOnly` -
mDNS discovery, no BLE advertising. This is the same approach the Matter
library itself uses after the last fabric is removed (see the `kFabricRemoved`
handler in the library's `Matter.cpp`).

Confirm it on the serial log at boot:

```
Matter: commissioning over the network (DNS-SD), BLE off.
```

Commission by entering the manual pairing code (or scanning the QR) from the
Settings page. Apple Home, Google Home and Home Assistant all support
on-network commissioning for a device already on the LAN.

`WiFi.setSleep(false)` is also applied in `setupWiFi()` - Arduino defaults Wi-Fi
to modem sleep, which compounds any remaining airtime loss.

Note that the Arduino Matter API itself exposes no way to close the
commissioning window (only `begin()`, `decommission()` and queries), which is
why `matter_handler.cpp` reaches through to the underlying CHIP
`CommissioningWindowManager` directly - under the CHIP stack lock, since that
API is not thread-safe.

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

Matter + Wi-Fi + BLE + mDNS + WebServer is a large binary, so most of the flash
goes to the app. If a build ever fails to boot with `Image length ... doesn't
fit in partition length` / `No bootable app partition`, the app outgrew its
partition - that message comes from the bootloader, not from the upload.

The `spiffs` partition keeps that name and SubType deliberately even though it
is formatted as LittleFS: both `LittleFS.begin()` and `uploadfs` locate the
partition by subtype, and naming it `littlefs` trips a warning in
`gen_esp32part.py`.

## Configuration (`platformio.ini`)

The project is configured for the `esp32c3` environment with required libraries defined in the `.pio` configuration file.

## Architecture Overview

- `src/main.cpp`: Entry point initializing all subsystems and starting the main loop.
- `src/discovery.*`: SSDP presence broadcasting and unique lamp ID generation from efuse MAC.
- `src/light_control.*`: PWM dimming, gamma correction, button debouncing, and radio toggle logic.
- `src/matter_handler.*`: Matter protocol implementation for smart home integration.
- `src/mqtt_handler.*`: MQTT client setup, state publishing, and Home Assistant discovery.
- `src/web_server.*`: Embedded HTTP server with LittleFS storage and fallback AP mode handling.
- `include/`: Shared headers for project-wide constants and declarations.

## Library Dependencies

The project uses the following PlatformIO libraries (defined in `platformio.ini`):

- **ESPmDNS** – mDNS/SSDP discovery support.
- **ArduinoJson** – JSON serialization for SSDP requests and MQTT payloads.
- **PubSubClient** – MQTT client library.
- **WebServer**, **DNSServer**, **LittleFS** – HTTP server, DNS handling, and persistent file storage.
