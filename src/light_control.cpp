#include "light_control.h"
#include "config.h"
#include "connection_stack_manager.h"
#include <Arduino.h>

// Arduino-ESP32 3.x uses a pin-based LEDC API; 2.x is channel-based. These
// wrappers let the same code build and run on both cores.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void pwmAttach() { ledcAttach(LED_PIN, PWM_FREQ, PWM_RES); }
static void pwmWrite(uint32_t duty) { ledcWrite(LED_PIN, duty); }
#else
#define PWM_CHANNEL 0
static void pwmAttach() {
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
    ledcAttachPin(LED_PIN, PWM_CHANNEL);
}
static void pwmWrite(uint32_t duty) { ledcWrite(PWM_CHANNEL, duty); }
#endif

// Default no-op - overridden by a connection stack's .cpp if one is linked
// in (see light_control.h).
__attribute__((weak)) void onLightStateChanged() {}

bool dimDirectionUp = true;

static bool lastBtnState = LOW;
static bool btnState = LOW;
static unsigned long lastDebounceTime = 0;
static unsigned long buttonPressTime = 0;
static bool isHolding = false;
static bool holdHandled = false;

static int rapidPressCount = 0;
static unsigned long rapidPressWindowStart = 0;

// Perceptually-linear dimming: raw PWM level (0-255) -> gamma-corrected duty
// cycle (0 - (2^PWM_RES)-1). Without this, brightness changes feel bunched
// up at the low end and barely-there at the high end.
uint32_t gammaCorrect(uint8_t level) {
    const uint32_t dutyMax = (1UL << PWM_RES) - 1;
    float normalized = (float)level / 255.0f;
    float corrected = pow(normalized, GAMMA);
    return (uint32_t)(corrected * dutyMax + 0.5f);
}

void initLightControl() {
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    pwmAttach();  // attach LEDC once with frequency + resolution
}

bool isButtonPressed() {
    return btnState == HIGH;
}

void applyPWM(bool notify) {
    uint32_t duty = 0;
    if (ledOn) {
        currentPWM = constrain(currentPWM, settings.minBrightness, settings.maxBrightness);
        duty = gammaCorrect((uint8_t)currentPWM);
    }
    pwmWrite(duty);

    static uint32_t lastLoggedDuty = UINT32_MAX;
    if (duty != lastLoggedDuty) {
        lastLoggedDuty = duty;
        Serial.printf("[LED] %s - duty %u/%u (pwm %d) on GPIO %d\n",
                      ledOn ? "ON" : "OFF", duty, (1UL << PWM_RES) - 1, currentPWM, LED_PIN);
    }

    if (notify) {
        onLightStateChanged();
    }
}

void blinkConfirm(int times, int gapMs) {
    const uint32_t fullDuty = (1UL << PWM_RES) - 1;
    for (int i = 0; i < times; i++) {
        pwmWrite(fullDuty);
        delay(gapMs);
        pwmWrite(0);
        delay(gapMs);
    }
    // Restore the LED to its actual state after the confirmation blink.
    applyPWM(false);
}

void toggleWiFiRadio() {
    if (settings.wifiRadioOff) {
        blinkConfirm(2, 500);
        settings.wifiRadioOff = false;
        saveSettings();
        // If the radio was off at boot, no connection stack was started at
        // all - start the one the settings select now that WiFi is back.
        ConnectionStackManager::startConfiguredStack();
    } else {
        blinkConfirm(4, 100);
        settings.wifiRadioOff = true;
        saveSettings();
    }
}

void handleButton() {
    bool reading = digitalRead(BUTTON_PIN);
    if (reading != lastBtnState) {
        lastDebounceTime = millis();
        Serial.printf("[BTN] raw GPIO %d -> %s\n", BUTTON_PIN, reading ? "HIGH" : "LOW");
    }

    if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
        if (reading != btnState) {
            btnState = reading;
            if (btnState == HIGH) {
                Serial.println("[BTN] press detected");
                buttonPressTime = millis();
                holdHandled = false;
            } else {
                if (isHolding) {
                    isHolding = false;
                    dimDirectionUp = !dimDirectionUp;
                    Serial.printf("[BTN] hold released - next hold dims %s\n", dimDirectionUp ? "up" : "down");
                    settings.lastPWM = currentPWM;
                    saveSettings();
                } else if (!holdHandled) {
                    Serial.println("[BTN] click - toggling light");
                    ledOn = !ledOn;
                    applyPWM(true);
                    settings.lastPWM = currentPWM;
                    saveSettings();

                    unsigned long now = millis();
                    if (now - rapidPressWindowStart > RAPID_PRESS_WINDOW_MS) {
                        rapidPressWindowStart = now;
                        rapidPressCount = 1;
                    } else {
                        rapidPressCount++;
                    }

                    if (rapidPressCount >= RAPID_PRESS_COUNT) {
                        rapidPressCount = 0;
                        toggleWiFiRadio();
                    }
                }
            }
        }
    }

    if (btnState == HIGH && !holdHandled) {
        if ((millis() - buttonPressTime) > HOLD_MS) {
            if (!isHolding) Serial.printf("[BTN] hold - dimming %s\n", dimDirectionUp ? "up" : "down");
            isHolding = true;
            ledOn = true;

            static unsigned long lastDimTime = 0;
            if (millis() - lastDimTime >= (unsigned long)settings.dimSpeed) {
                lastDimTime = millis();

                if (dimDirectionUp) {
                    if (currentPWM < settings.maxBrightness) currentPWM++;
                    if (currentPWM > settings.maxBrightness) currentPWM = settings.maxBrightness;
                } else {
                    if (currentPWM > settings.minBrightness) currentPWM--;
                    if (currentPWM < settings.minBrightness) currentPWM = settings.minBrightness;
                }
                applyPWM(true);
            }
        }
    }
    lastBtnState = reading;
}