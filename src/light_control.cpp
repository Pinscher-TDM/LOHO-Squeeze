#include "light_control.h"
#include "config.h"

#include "web_server.h"
#include "mqtt_handler.h"
#include "matter_handler.h"
#include <Arduino.h>

bool ledOn = false;
int currentPWM = 128;
bool dimDirectionUp = true;

static bool lastBtnState = LOW;
static bool btnState = LOW;
static unsigned long lastDebounceTime = 0;
static unsigned long buttonPressTime = 0;
static bool isHolding = false;
static bool holdHandled = false;

static int rapidPressCount = 0;
static unsigned long rapidPressWindowStart = 0;

uint32_t gammaCorrect(uint8_t level) {
    const uint32_t dutyMax = (1UL << PWM_RES) - 1;
    float normalized = (float)level / 255.0f;
    float corrected = pow(normalized, GAMMA);
    return (uint32_t)(corrected * dutyMax + 0.5f);
}

void initLightControl() {
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);

    // BUG FIX: the LEDC channel was never attached to the pin anywhere in
    // the project, so ledcWrite() had nothing to actually drive. This is
    // the one-time setup call that was missing.
    ledcAttach(LED_PIN, PWM_FREQ, PWM_RES);

    ledOn = settings.ledOn;
    currentPWM = settings.lastPWM;

    // BUG FIX: the saved on/off + brightness state was loaded into
    // variables but never actually applied to the LED at boot.
    applyPWM(false, false);
}

// BUG FIX: original logic only ran the ledcWrite when the light was OFF
// (and wrote the raw brightness, not 0), and did nothing at all when the
// light was ON. This restores the correct on/off + gamma-corrected duty.
void applyPWM(bool publish, bool updateMatter) {
    if (!ledOn) {
        ledcWrite(LED_PIN, 0);
    } else {
        currentPWM = constrain(currentPWM, settings.minBrightness, settings.maxBrightness);
        ledcWrite(LED_PIN, gammaCorrect((uint8_t)currentPWM));
    }

    if (publish) {
        publishMQTTState();
    }
    if (updateMatter) {
        syncMatterState();
    }
}

void blinkConfirm(int times, int gapMs) {
    // BUG FIX: this wrote settings.maxBrightness (0-255) directly as a duty
    // cycle against a 10-bit (0-1023) channel, so "full brightness" was
    // actually only ~25% duty. Use the real max duty for the confirm blink.
    const uint32_t fullDuty = (1UL << PWM_RES) - 1;
    for (int i = 0; i < times; i++) {
        ledcWrite(LED_PIN, fullDuty);
        delay(gapMs);
        ledcWrite(LED_PIN, 0);
        delay(gapMs);
    }
    // Restore the LED to its actual state after the confirmation blink.
    applyPWM(false, false);
}

void toggleWiFiRadio() {
    if (settings.wifiRadioOff) {
        blinkConfirm(2, 150);
        settings.wifiRadioOff = false;
        saveSettings();
        setupWiFi();
        // BUG FIX: if the radio was off at boot, the web server and MQTT
        // were never started at all - re-arm them now that WiFi is back.
        // initWebServer()/setupMQTT() are both now idempotent (safe to
        // call again if they already started).
        initWebServer();
        setupMQTT();
    } else {
        blinkConfirm(4, 100);
        settings.wifiRadioOff = true;
        saveSettings();
    }
}

void handleButton() {
    bool reading = digitalRead(BUTTON_PIN);
    if (reading != lastBtnState) lastDebounceTime = millis();

    if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
        if (reading != btnState) {
            btnState = reading;
            if (btnState == HIGH) {
                buttonPressTime = millis();
                holdHandled = false;
            } else {
                if (isHolding) {
                    isHolding = false;
                    dimDirectionUp = !dimDirectionUp;
                    saveSettings();
                } else if (!holdHandled) {
                    ledOn = !ledOn;
                    applyPWM(true, true);
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
            isHolding = true;
            ledOn = true;

            static unsigned long lastDimTime = 0;
            if (millis() - lastDimTime >= (unsigned long)settings.dimSpeed) {
                lastDimTime = millis();

                // BUG FIX: this block previously did nothing at all -
                // holding the button never changed currentPWM. Restored
                // the one-direction-per-hold dimming logic here.
                if (dimDirectionUp) {
                    if (currentPWM < settings.maxBrightness) currentPWM++;
                    if (currentPWM > settings.maxBrightness) currentPWM = settings.maxBrightness;
                } else {
                    if (currentPWM > settings.minBrightness) currentPWM--;
                    if (currentPWM < settings.minBrightness) currentPWM = settings.minBrightness;
                }
                applyPWM(true, true);
            }
        }
    }
    lastBtnState = reading;
}
