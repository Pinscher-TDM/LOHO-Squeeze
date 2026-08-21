#include "matter_handler.h"
#include "config.h"
#include "light_control.h"
#include <WiFi.h>
#include <Matter.h>

static MatterDimmableLight matterLight;
static bool matterStarted = false;

// Called when a Matter controller (HomeKit, Google Home, Home Assistant,
// etc.) sends an on/off write to this device.
static bool onMatterOnOffChange(bool state) {
    ledOn = state;
    applyPWM(true, false); // publish to MQTT too; don't re-echo back into Matter
    saveSettings();
    return true; // Return true to indicate success
}

// Called when a Matter controller sends a brightness write.
static bool onMatterBrightnessChange(uint8_t level) {
    currentPWM = constrain((int)level, settings.minBrightness, settings.maxBrightness);
    ledOn = true;
    applyPWM(true, false);
    saveSettings();
    return true; // Return true to indicate success
}

void setupMatter() {
    if (matterStarted) return;               // already running - idempotent
    if (!settings.matterEnabled) return;      // disabled in settings
    if (WiFi.status() != WL_CONNECTED) return; // Matter over Wi-Fi needs an IP network

    matterLight.begin(ledOn, (uint8_t)constrain(currentPWM, 0, 254));
    matterLight.onChangeOnOff(onMatterOnOffChange);
    matterLight.onChangeBrightness(onMatterBrightnessChange);

    Matter.begin();
    matterStarted = true;

    if (!Matter.isDeviceCommissioned()) {
        Serial.println("Matter: not commissioned yet.");
        Serial.print("Manual pairing code: ");
        Serial.println(Matter.getManualPairingCode());
        Serial.print("QR code URL: ");
        Serial.println(Matter.getOnboardingQRCodeUrl());
    } else {
        Serial.println("Matter: already commissioned onto at least one fabric.");
    }
}

void syncMatterState() {
    if (!matterStarted) return;
    if (matterLight.getOnOff() != ledOn) {
        matterLight.setOnOff(ledOn);
    }
    uint8_t level = (uint8_t)constrain(currentPWM, 0, 254);
    if (matterLight.getBrightness() != level) {
        matterLight.setBrightness(level);
    }
}

bool isMatterStarted() {
    return matterStarted;
}

bool isMatterCommissioned() {
    if (!matterStarted) return false;
    return Matter.isDeviceCommissioned();
}

String getMatterPairingCode() {
    if (!matterStarted) return "";
    return Matter.getManualPairingCode();
}

String getMatterQRCodeUrl() {
    if (!matterStarted) return "";
    return Matter.getOnboardingQRCodeUrl();
}

void openMatterCommissioningWindow() {
    if (!matterStarted) return;
    // Method name may differ across core versions - if this doesn't
    // compile, check your installed core's Matter example for the
    // current name (some versions expose this directly on Matter,
    // others via a session/fabric manager helper).
    Matter.openCommissioningWindow();
}
