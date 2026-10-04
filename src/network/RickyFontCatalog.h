#pragma once
#ifdef RICKYOS_PRODUCT

#include "CrossPointSettings.h"

// RickyOS downloads fonts from its own catalog, github.com/chinoryunqin/RickyOS-fonts,
// built by scripts/build_rickyos_font_catalog.py. Both manifests list the same files:
// one is a GitHub release asset, the other is served through jsDelivr, usually the
// faster route from mainland China. Each manifest's baseUrl stays on its own host, so
// the font files come over the route that served the list; its "mirrors" name the
// other hosts, which FontDownloadActivity tries when a file fails.
namespace RickyFontCatalog {

inline constexpr char GITHUB_MANIFEST[] =
    "https://github.com/chinoryunqin/RickyOS-fonts/releases/latest/download/fonts.json";
inline constexpr char MIRROR_MANIFEST[] = "https://cdn.jsdelivr.net/gh/chinoryunqin/RickyOS-fonts@latest/mirror.json";
inline constexpr char FASTLY_MANIFEST[] =
    "https://fastly.jsdelivr.net/gh/chinoryunqin/RickyOS-fonts@latest/mirror.json";
inline constexpr int MANIFEST_COUNT = 3;

// The manifest to try at each attempt; the China profile starts on jsDelivr.
constexpr const char* manifestFor(const CrossPointSettings::ContentProfile profile, const int attempt) {
  constexpr const char* china[MANIFEST_COUNT] = {MIRROR_MANIFEST, FASTLY_MANIFEST, GITHUB_MANIFEST};
  constexpr const char* global[MANIFEST_COUNT] = {GITHUB_MANIFEST, MIRROR_MANIFEST, FASTLY_MANIFEST};
  return (profile == CrossPointSettings::ContentProfile::China ? china : global)[attempt];
}

static_assert(manifestFor(CrossPointSettings::ContentProfile::China, 0) == MIRROR_MANIFEST);
static_assert(manifestFor(CrossPointSettings::ContentProfile::China, 2) == GITHUB_MANIFEST);
static_assert(manifestFor(CrossPointSettings::ContentProfile::Global, 0) == GITHUB_MANIFEST);
static_assert(manifestFor(CrossPointSettings::ContentProfile::Global, 2) == FASTLY_MANIFEST);

}  // namespace RickyFontCatalog
#endif
