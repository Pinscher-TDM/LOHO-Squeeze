#include "matter_handler.h"
#include "config.h"
#include "light_control.h"
#include <WiFi.h>
#include <Matter.h>
#include <app/server/Server.h>
#include <platform/PlatformManager.h>

// How long a commissioning window stays open. Matter caps this at 900s.
static const uint16_t COMMISSION_WINDOW_S = 600;

// Re-open the commissioning window advertising over DNS-SD ONLY (no BLE).
//
// Why: the ESP32-C3 has a single radio shared between Wi-Fi and BLE. Matter
// advertises over BLE continuously while uncommissioned, and coexistence
// arbitration starves Wi-Fi badly enough that HTTP reads time out with EAGAIN
// and the Arduino core drops the connection - the web UI becomes unreachable
// for as long as Matter is enabled and unpaired.
//
// We never need BLE: setupMatter() only runs once WiFi.status() is
// WL_CONNECTED, so the device always already has IP connectivity and can be
// commissioned on-network. This is the same approach the Matter library itself
// takes after the last fabric is removed (see the kFabricRemoved handler in
// Matter.cpp), for exactly the same reason.
//
// CHIP is not thread-safe; these calls must hold the stack lock.
static bool openDnssdOnlyCommissioning() {
    bool ok = false;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    {
        chip::CommissioningWindowManager &mgr =
            chip::Server::GetInstance().GetCommissioningWindowManager();

        // Matter.begin() already opened a BLE-advertising window; close it
        // first or the reopen is refused.
        if (mgr.IsCommissioningWindowOpen()) {
            mgr.CloseCommissioningWindow();
        }
        CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(
            chip::System::Clock::Seconds16(COMMISSION_WINDOW_S),
            chip::CommissioningWindowAdvertisement::kDnssdOnly);
        ok = (err == CHIP_NO_ERROR);
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    return ok;
}

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
        if (openDnssdOnlyCommissioning()) {
            Serial.println("Matter: commissioning over the network (DNS-SD), BLE off.");
        } else {
            Serial.println("Matter: WARNING - could not switch to DNS-SD-only "
                           "commissioning; BLE advertising may still be active.");
        }
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
    
    // Check if the device is already commissioned
    if (Matter.isDeviceCommissioned()) {
        Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
        return;
    }
    
    // Re-arm the window (it expires after COMMISSION_WINDOW_S) and keep it
    // DNS-SD-only so pressing this button never brings BLE back up.
    openDnssdOnlyCommissioning();

    // If not commissioned, show pairing information
    Serial.println("");
    Serial.println("Matter Node is not commissioned yet.");
    Serial.println("Initiate the device discovery in your Matter environment.");
    Serial.println("Commission it to your Matter hub with the manual pairing code or QR code");
    Serial.printf("Manual pairing code: %s\r\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR code URL: %s\r\n", Matter.getOnboardingQRCodeUrl().c_str());
}
