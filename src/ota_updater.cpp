#include "ota_updater.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_partition.h>
#include <LittleFS.h>

static const char* USER_AGENT = "LOHO-Squeeze-OTA";
std::vector<GitHubRelease> cachedReleases;

bool fetchGitHubReleases(std::vector<GitHubRelease>& out, String& errorOut) {
    out.clear();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    String url = String("https://api.github.com/repos/") + GITHUB_OWNER + "/" + GITHUB_REPO + "/releases";

    if (!https.begin(client, url)) {
        errorOut = "could not open connection to GitHub";
        return false;
    }
    https.addHeader("User-Agent", USER_AGENT);
    https.addHeader("Accept", "application/vnd.github+json");

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        errorOut = "GitHub API returned HTTP " + String(code);
        https.end();
        return false;
    }

    // Filter for minimal fields
    JsonDocument filter;
    JsonObject filterItem = filter.to<JsonArray>().add<JsonObject>();
    filterItem["tag_name"] = true;
    filterItem["name"] = true;
    filterItem["published_at"] = true;
    JsonObject assetFilterItem = filterItem["assets"].to<JsonArray>().add<JsonObject>();
    assetFilterItem["name"] = true;
    assetFilterItem["browser_download_url"] = true;
    assetFilterItem["size"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
    https.end();

    if (err) {
        errorOut = String("failed to parse GitHub response: ") + err.c_str();
        return false;
    }

    for (JsonObject rel : doc.as<JsonArray>()) {
        GitHubRelease r;
        r.tag = rel["tag_name"] | "";
        r.name = rel["name"] | "";
        if (r.name.length() == 0) r.name = r.tag;
        r.publishedAt = rel["published_at"] | "";

        // Iterate assets to find firmware and LittleFS images
        for (JsonObject asset : rel["assets"].as<JsonArray>()) {
            String assetName = asset["name"] | "";
            if (assetName.endsWith(".bin")) {
                String url = asset["browser_download_url"] | "";
                size_t size = asset["size"] | 0;
                // Heuristic: firmware images usually contain "firmware" or "app"
                if (assetName.indexOf("firmware") >= 0 || assetName.indexOf("app") >= 0) {
                    r.assetAppUrl = url;
                    r.assetAppSize = size;
                } else if (assetName.indexOf("littlefs") >= 0 || assetName.indexOf("spiffs") >= 0) {
                    r.assetFsUrl = url;
                    r.assetFsSize = size;
                } else {
                    // If no explicit match, first .bin is treated as firmware
                    if (r.assetAppUrl.isEmpty()) {
                        r.assetAppUrl = url;
                        r.assetAppSize = size;
                    }
                }
            }
        }
        out.push_back(r);
    }
    return true;
}

bool performOTAUpdate(const GitHubRelease& release, String& errorOut) {
    if (release.assetAppUrl.length() == 0) {
        errorOut = "No firmware (.bin) asset attached to this release";
        return false;
    }

    // 1. Download and flash the app firmware
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!https.begin(client, release.assetAppUrl)) {
        errorOut = "Could not open connection to download server";
        return false;
    }
    https.addHeader("User-Agent", USER_AGENT);

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        errorOut = "Download returned HTTP " + String(code);
        https.end();
        return false;
    }

    int len = https.getSize();
    if (len <= 0) {
        errorOut = "Server didn't report a firmware size";
        https.end();
        return false;
    }

    if (!Update.begin(len)) {
        errorOut = Update.errorString();
        https.end();
        return false;
    }

    Serial.printf("[OTA] Downloading app firmware %s (%d bytes)...\n", release.tag.c_str(), len);
    Stream* stream = https.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    https.end();

    if (written != (size_t)len) {
        errorOut = "Only wrote " + String(written) + "/" + String(len) + " bytes";
        Update.abort();
        return false;
    }
    if (!Update.end(true)) {
        errorOut = Update.errorString();
        return false;
    }
    Serial.printf("[OTA] App firmware flashed successfully.\n");

    // 2. If a filesystem image is provided, flash it
    if (release.assetFsUrl.length() > 0) {
        // Unmount LittleFS to avoid conflicts
        LittleFS.end();

        const esp_partition_t* fsPart = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
            "spiffs"
        );
        if (!fsPart) {
            errorOut = "SPIFFS partition not found";
            return false;
        }
        if (release.assetFsSize > fsPart->size) {
            errorOut = "FS image size (" + String(release.assetFsSize) + 
                       ") exceeds partition size (" + String(fsPart->size) + ")";
            return false;
        }

        WiFiClientSecure fsClient;
        fsClient.setInsecure();
        HTTPClient fsHttps;
        fsHttps.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!fsHttps.begin(fsClient, release.assetFsUrl)) {
            errorOut = "Could not connect to FS download server";
            return false;
        }
        fsHttps.addHeader("User-Agent", USER_AGENT);
        int fsCode = fsHttps.GET();
        if (fsCode != HTTP_CODE_OK) {
            errorOut = "FS download returned HTTP " + String(fsCode);
            fsHttps.end();
            return false;
        }
        int fsLen = fsHttps.getSize();
        if (fsLen <= 0) {
            errorOut = "Server didn't report FS image size";
            fsHttps.end();
            return false;
        }

        // Use Update to write to the spiffs partition
        if (!Update.begin(fsLen, U_FLASH, U_SPIFFS)) {
            errorOut = Update.errorString();
            fsHttps.end();
            return false;
        }
        Serial.printf("[OTA] Downloading FS image (%d bytes)...\n", fsLen);
        Stream* fsStream = fsHttps.getStreamPtr();
        size_t fsWritten = Update.writeStream(*fsStream);
        fsHttps.end();
        if (fsWritten != (size_t)fsLen) {
            errorOut = "FS write mismatch: " + String(fsWritten) + "/" + String(fsLen);
            Update.abort();
            return false;
        }
        if (!Update.end(true)) {
            errorOut = Update.errorString();
            return false;
        }
        Serial.printf("[OTA] FS image flashed successfully.\n");
    }

    Serial.printf("[OTA] All updates completed for %s - ready to reboot.\n", release.tag.c_str());
    return true;
}