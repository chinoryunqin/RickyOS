#pragma once
#ifdef RICKYOS_PRODUCT

#include "CrossPointSettings.h"

// RickyOS downloads fonts from its own catalog, github.com/chinoryunqin/RickyOS-fonts,
// built by scripts/build_rickyos_font_catalog.py. Both manifests list the same files:
// one is a GitHub release asset, the other is served through jsDelivr, usually the
// faster route from mainland China. Each manifest's baseUrl stays on its own host, so
// the font files come over the route that served the list.
namespace RickyFontCatalog {

inline constexpr char GITHUB_MANIFEST[] =
    "https://github.com/chinoryunqin/RickyOS-fonts/releases/latest/download/fonts.json";
inline constexpr char MIRROR_MANIFEST[] = "https://cdn.jsdelivr.net/gh/chinoryunqin/RickyOS-fonts@latest/mirror.json";
inline constexpr int MANIFEST_COUNT = 2;

// The manifest to try at each attempt; the China profile starts on the mirror.
constexpr const char* manifestFor(const CrossPointSettings::ContentProfile profile, const int attempt) {
  const bool mirrorFirst = profile == CrossPointSettings::ContentProfile::China;
  return (attempt == 0) == mirrorFirst ? MIRROR_MANIFEST : GITHUB_MANIFEST;
}

static_assert(manifestFor(CrossPointSettings::ContentProfile::China, 0) == MIRROR_MANIFEST);
static_assert(manifestFor(CrossPointSettings::ContentProfile::China, 1) == GITHUB_MANIFEST);
static_assert(manifestFor(CrossPointSettings::ContentProfile::Global, 0) == GITHUB_MANIFEST);
static_assert(manifestFor(CrossPointSettings::ContentProfile::Global, 1) == MIRROR_MANIFEST);

}  // namespace RickyFontCatalog
#endif
