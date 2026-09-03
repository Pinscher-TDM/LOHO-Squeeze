# LOHO Squeeze - Barebones

A stripped-down build of LOHO Squeeze for an ESP32-C3: physical-button
dimming with brightness memory, a small web dashboard, and self-update
from your GitHub Releases page. The MQTT / HomeSpan / KNX connectivity
stacks from the full project have been removed but the code is structured
so they can be dropped back in without a rewrite (see "Adding stacks back"
below).

## Features

- **Physical button**: click to toggle, hold to dim. Dimming uses a
  gamma-corrected curve (`GAMMA` in `src/config.h`) so brightness changes
  feel linear to the eye instead of bunching up at one end. The last
  on/off state and brightness are saved to flash (NVS) and restored on
  boot.
- **Web dashboard**: served from `data/` (`index.html`, `settings.html`,
  `update.html`, `style.css`) via an embedded HTTP server + LittleFS.
  Falls back to a `LOHO-Squeeze` Wi-Fi AP if it can't join your network,
  and reconnects automatically in the background once your network is
  reachable again.
- **Self-update from GitHub Releases**: the `/update` page lists every
  release from
  [Pinscher-TDM/LOHO-Squeeze/releases](https://github.com/Pinscher-TDM/LOHO-Squeeze/releases)
  with a firmware file attached, and lets you install any of them
  (upgrade *or* roll back) with one click.

## Getting Started

Uses PlatformIO.

1. `git clone <repo_url>`, open in VS Code with the PlatformIO extension.
2. Wire the LED to `LED_PIN` and the button to `BUTTON_PIN` (`src/config.h`, GPIO 3 / 4 by default).
3. Build and upload:
   ```bash
   pio run -t upload      # firmware
   pio run -t uploadfs    # data/ -> LittleFS partition (the web pages)
   ```
   Both steps are needed the first time - skip `uploadfs` and every page
   returns "Filesystem not mounted".
4. On first boot with no saved Wi-Fi credentials, connect to the
   `LOHO-Squeeze` AP and open `http://192.168.4.1/settings` to set your SSID/password.

## Publishing an OTA-installable release

The update page only shows an "Install" button for releases that have a
**firmware `.bin` asset attached** (any file ending in `.bin` - it looks
for the first match). Releases without one are still listed, just not
installable, so changelog-only releases are fine.

To publish one:
```bash
pio run                              # builds .pio/build/esp32c3/firmware.bin
```
Then create a GitHub release (tag it, e.g. `v1.1.0`) and attach
`firmware.bin` as a release asset. That's it - the device will see it next
time someone opens `/update`.

Note: OTA only replaces the **firmware**, not the web pages in `data/`.
If you change `data/*`, ship a new `uploadfs` manually (or extend
`src/ota_updater.cpp` to also fetch and write a filesystem image - the
`spiffs` partition is separate from `ota_0`/`ota_1` for exactly this
reason).

## Flash layout (`partitions.csv`)

Unlike the original factory-only build, this one needs two app slots to
support OTA:

| Partition | Offset | Size |
| --- | --- | --- |
| `nvs` | `0x9000` | 20 KB |
| `otadata` | `0xe000` | 8 KB |
| `app0` (`ota_0`) | `0x10000` | 1280 KB |
| `app1` (`ota_1`) | `0x150000` | 1280 KB |
| `spiffs` (LittleFS) | `0x290000` | 1472 KB |

`Update.h` picks the inactive slot automatically - you don't need to
track which one is "current". If you ever change `partitions.csv`, erase
the chip first (`pio run -t erase`); NVS lives at a fixed offset, so a
re-layout leaves stale settings that decode as garbage.

## Architecture

- `src/main.cpp` - entry point, settings load/save (Preferences/NVS), main loop.
- `src/config.h` - pins, tunables, and `AppSettings` (persisted settings struct).
- `src/light_control.*` - PWM dimming, gamma correction, button debounce/hold/click, brightness memory.
- `src/connection_stack_manager.*` - single-active-stack selector. Barebones: only `WEB_SERVER`.
- `src/web_server.*` - HTTP server, LittleFS-served pages, fallback AP, settings API, OTA API.
- `src/ota_updater.*` - talks to the GitHub Releases API, downloads and flashes a chosen release.
- `data/` - the actual web pages (`index.html`, `settings.html`, `update.html`, `style.css`).

### Adding stacks back (MQTT / HomeSpan / KNX)

The connection-stack pattern from the full project is intentionally kept
even though only one stack (`WEB_SERVER`) is implemented here. To bring a
stack back:

1. Add its handler files under `src/` (e.g. `mqtt_handler.h/.cpp`).
2. Add a value for it to `ConnectionState` in `src/connection_stack_manager.h`.
3. Add a branch for it in `ConnectionStackManager::startConfiguredStack()`
   (`src/connection_stack_manager.cpp`).
4. Add a `case ConnectionState::XXX: handleXxx(); break;` in `main.cpp`'s `loop()`.
5. Add its settings fields to `AppSettings` in `src/config.h`, plus
   load/save lines in `main.cpp`, plus a toggle in `data/settings.html`.
6. If it needs to react to light state changes, define a non-weak
   `onLightStateChanged()` in its `.cpp` - `src/light_control.cpp` already
   calls this on every state change and has a no-op default, so nothing
   there needs editing.
7. Add its library back to `platformio.ini`'s `lib_deps` (e.g.
   `knolleary/PubSubClient@^2.8`).
