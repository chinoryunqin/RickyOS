#pragma once
#ifdef RICKYOS_PRODUCT

#include <algorithm>
#include <cstdint>

#include "CrossPointSettings.h"

// RickyOS default standby pictures, github.com/chinoryunqin/RickyOS-wallpapers, built by
// scripts/build_rickyos_wallpapers.py. Same routes as RickyFontCatalog: a GitHub release
// manifest and a jsDelivr one (two CDN hosts); each lists the other hosts under "mirrors".
namespace RickyWallpaperCatalog {

inline constexpr char GITHUB_MANIFEST[] =
    "https://github.com/chinoryunqin/RickyOS-wallpapers/releases/latest/download/wallpapers.json";
inline constexpr char MIRROR_MANIFEST[] =
    "https://cdn.jsdelivr.net/gh/chinoryunqin/RickyOS-wallpapers@latest/mirror.json";
inline constexpr char FASTLY_MANIFEST[] =
    "https://fastly.jsdelivr.net/gh/chinoryunqin/RickyOS-wallpapers@latest/mirror.json";
inline constexpr int MANIFEST_COUNT = 3;
// Card folder the pictures land in; the standby picker starts in /images.
inline constexpr char FOLDER[] = "/images/待机图片";

constexpr const char* manifestFor(const CrossPointSettings::ContentProfile profile, const int attempt) {
  constexpr const char* china[MANIFEST_COUNT] = {MIRROR_MANIFEST, FASTLY_MANIFEST, GITHUB_MANIFEST};
  constexpr const char* global[MANIFEST_COUNT] = {GITHUB_MANIFEST, MIRROR_MANIFEST, FASTLY_MANIFEST};
  return (profile == CrossPointSettings::ContentProfile::China ? china : global)[attempt];
}

// The release in a manifest's baseUrl ("...@v1.1.0/..." or ".../download/v1.1.0/") as
// 0xMMmmpp, 0 when absent, so manifests from routes with stale caches can be ranked.
constexpr uint32_t releaseOf(const char* url) {
  for (const char* p = url; p && *p; ++p) {
    if ((*p != '@' && *p != '/') || p[1] != 'v' || p[2] < '0' || p[2] > '9') continue;
    uint32_t parts[3] = {0, 0, 0};
    int part = 0;
    for (const char* q = p + 2; *q && part < 3; ++q) {
      if (*q >= '0' && *q <= '9') {
        parts[part] = parts[part] * 10 + static_cast<uint32_t>(*q - '0');
      } else if (*q == '.') {
        ++part;
      } else {
        break;
      }
    }
    return (std::min<uint32_t>(parts[0], 255) << 16) | (std::min<uint32_t>(parts[1], 255) << 8) |
           std::min<uint32_t>(parts[2], 255);
  }
  return 0;
}
static_assert(releaseOf("https://cdn.jsdelivr.net/gh/x/y@v1.1.0/wallpapers/") == 0x010100);
static_assert(releaseOf("https://github.com/x/y/releases/download/v1.0.0/") == 0x010000);
static_assert(releaseOf("https://example.com/plain/") == 0);

}  // namespace RickyWallpaperCatalog
#endif
