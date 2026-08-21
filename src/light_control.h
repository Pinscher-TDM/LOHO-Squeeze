#ifndef LIGHT_CONTROL_H
#define LIGHT_CONTROL_H

#include <Arduino.h>

extern bool ledOn;
extern int currentPWM;
extern bool dimDirectionUp;

void initLightControl();
void applyPWM(bool publish, bool updateMatter);
void handleButton();
void toggleWiFiRadio();
void blinkConfirm(int times, int gapMs);

#endif