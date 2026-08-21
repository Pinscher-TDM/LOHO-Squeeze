#ifndef MATTER_HANDLER_H
#define MATTER_HANDLER_H

#include <Arduino.h>

// NOTE: Requires the pioarduino platform (see platformio.ini) - it ships
// Arduino-ESP32 core 3.x+, which is where MatterDimmableLight and Matter.*
// live. The API below matches the MatterDimmableLight example as of core
// 3.x; if method names differ on your installed core version, check
// File > Examples > Matter > MatterDimmableLight (or the pioarduino
// examples/arduino-matter-light project) for the current signatures.

// Call once Wi-Fi is connected (safe to call repeatedly - it only starts
// Matter once). Does nothing if settings.matterEnabled is false or Wi-Fi
// isn't connected yet.
void setupMatter();

// Push the current ledOn/currentPWM state out to the Matter attributes.
// Cheap to call - it only writes if the value actually changed, so it's
// safe to call on every LED update.
void syncMatterState();

bool isMatterStarted();
bool isMatterCommissioned();
String getMatterPairingCode();
String getMatterQRCodeUrl();

// Re-opens the commissioning window so the device can be added to another
// app/ecosystem (Matter supports being on multiple fabrics at once - e.g.
// Apple Home AND Home Assistant AND Google Home simultaneously - but each
// one has to commission it separately via its own "Add Device" flow).
void openMatterCommissioningWindow();

#endif
