#ifndef LIGHT_CONTROL_H
#define LIGHT_CONTROL_H

#include <Arduino.h>

void initLightControl();
void applyPWM(bool echo = false);
void handleButton();
void toggleWiFiRadio();
void blinkConfirm(int times, int gapMs);

#endif