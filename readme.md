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
