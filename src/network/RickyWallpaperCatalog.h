#pragma once
#ifdef RICKYOS_PRODUCT

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

}  // namespace RickyWallpaperCatalog
#endif
