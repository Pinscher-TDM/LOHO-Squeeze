#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include <Arduino.h>
#include <vector>

// The GitHub repo this firmware checks for updates against.
// https://github.com/Pinscher-TDM/LOHO-Squeeze/releases
#define GITHUB_OWNER "Pinscher-TDM"
#define GITHUB_REPO  "LOHO-Squeeze"

struct GitHubRelease {
    String tag;           // e.g. "v1.2.0"
    String name;          // release title (falls back to tag if blank)
    String publishedAt;   // ISO8601 date, straight from GitHub
    String assetAppUrl;   // direct download URL of the firmware .bin asset - empty if this release has none
    String assetFsUrl;    // direct download URL of the LittleFS .bin asset (optional)
    size_t assetAppSize = 0; // bytes, 0 if unknown
    size_t assetFsSize = 0;  // bytes, 0 if unknown
};

// Expose the shared release cache definition
extern std::vector<GitHubRelease> cachedReleases;

// Fetches the list of releases from the GitHub Releases API (newest first,
// same order as the releases page). Only releases with a .bin asset
// attached can actually be installed - assetAppUrl/assetFsUrl empty if none.
bool fetchGitHubReleases(std::vector<GitHubRelease>& out, String& errorOut);

// Downloads a release's firmware asset into the inactive OTA partition and,
// optionally, a filesystem image into the spiffs partition. On success,
// marks the app bootable (a reboot is still needed to actually run it).
// Returns false (with errorOut set) if anything goes wrong; the currently
// running firmware is left untouched.
bool performOTAUpdate(const GitHubRelease& release, String& errorOut);

#endif