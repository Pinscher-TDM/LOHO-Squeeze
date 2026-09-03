#ifndef LIGHT_CONTROL_H
#define LIGHT_CONTROL_H

#include <Arduino.h>

void initLightControl();

// notify=true means "this change should be broadcast to any connectivity
// stack that's listening" (see onLightStateChanged() below).
void applyPWM(bool notify = false);

void handleButton();
bool isButtonPressed();   // debounced state of the physical button
void toggleWiFiRadio();
void blinkConfirm(int times, int gapMs);

// Extension point for future connection stacks (MQTT, HomeSpan, KNX, ...).
// This is declared weak in light_control.cpp (a no-op by default). To hook
// in a stack, just define a normal (non-weak) version of this function in
// that stack's .cpp file - the linker will use it instead automatically.
void onLightStateChanged();

#endif
