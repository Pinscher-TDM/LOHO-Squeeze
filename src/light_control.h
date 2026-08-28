#ifndef LIGHT_CONTROL_H
#define LIGHT_CONTROL_H

#include <Arduino.h>

void initLightControl();
void applyPWM(bool echo = false);
void handleButton();
bool isButtonPressed();   // debounced state of the physical button
void toggleWiFiRadio();
void blinkConfirm(int times, int gapMs);

#endif