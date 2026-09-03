#include "ota_updater.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

static const char* USER_AGENT = "LOHO-Squeeze-OTA";

bool fetchGitHubReleases(std::vector<GitHubRelease>& out, String& errorOut) {
    out.clear();

    WiFiClientSecure client;
    // Barebones: skip certificate validation to avoid having to embed/keep
    // GitHub's root CA up to date. For a production build, pin GitHub's
    // root CA instead (client.setCACert(...)).
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

    // Filter so we only keep the fields we actually use - the full
    // response for a repo with many releases/assets is far more JSON than
    // we want to hold in RAM on a microcontroller.
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

        // Use the first asset ending in .bin as the firmware image for
        // this release. If you publish more than one .bin per release,
        // rename the firmware one so it sorts first, or tighten this match.
        for (JsonObject asset : rel["assets"].as<JsonArray>()) {
            String assetName = asset["name"] | "";
            if (assetName.endsWith(".bin")) {
                r.assetUrl = asset["browser_download_url"] | "";
                r.assetSize = asset["size"] | 0;
                break;
            }
        }

        out.push_back(r);
    }

    return true;
}

bool performOTAUpdate(const GitHubRelease& release, String& errorOut) {
    if (release.assetUrl.length() == 0) {
        errorOut = "this release has no firmware (.bin) asset attached";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // see note in fetchGitHubReleases()

    HTTPClient https;
    // GitHub asset downloads redirect from api/release-asset URLs to a
    // short-lived objects.githubusercontent.com URL - follow that automatically.
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!https.begin(client, release.assetUrl)) {
        errorOut = "could not open connection to download server";
        return false;
    }
    https.addHeader("User-Agent", USER_AGENT);

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        errorOut = "download returned HTTP " + String(code);
        https.end();
        return false;
    }

    int len = https.getSize();
    if (len <= 0) {
        errorOut = "server didn't report a firmware size";
        https.end();
        return false;
    }

    if (!Update.begin(len)) {
        errorOut = Update.errorString();
        https.end();
        return false;
    }

    Serial.printf("[OTA] Downloading %s (%d bytes)...\n", release.tag.c_str(), len);
    WiFiClient* stream = https.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    https.end();

    if (written != (size_t)len) {
        errorOut = "only wrote " + String(written) + "/" + String(len) + " bytes";
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        errorOut = Update.errorString();
        return false;
    }

    Serial.printf("[OTA] Flashed %s successfully - ready to reboot into it.\n", release.tag.c_str());
    return true;
}
