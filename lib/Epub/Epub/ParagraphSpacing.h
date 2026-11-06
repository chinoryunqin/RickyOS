#pragma once

#include <cstdint>

namespace paragraphSpacing {
constexpr uint16_t DEFAULT_PERCENT = 100;
constexpr uint16_t MAX_PERCENT = 400;
constexpr uint8_t LEGACY_MAX_PERCENT = 200;

// Old settings described additional line pitch; the new value is neutral at 100.
constexpr uint16_t fromAdditional(const int percent) {
  return percent >= 0 && percent <= LEGACY_MAX_PERCENT
             ? static_cast<uint16_t>(percent >= 100 ? LEGACY_MAX_PERCENT : DEFAULT_PERCENT + percent)
             : DEFAULT_PERCENT;
}
constexpr uint16_t fromLegacy(const int level) {
  constexpr uint8_t additional[] = {0, 50, 75, 100, 125, 150};
  return level >= 0 && level < 6 ? fromAdditional(additional[level]) : DEFAULT_PERCENT;
}

// Scale an authored paragraph margin. With no authored gap, values above 100%
// add up to three line pitches; lower values never overlap adjacent lines.
constexpr int marginPixels(const int authored, const int lineHeight, const uint16_t percent) {
  const int scale = percent <= MAX_PERCENT ? percent : MAX_PERCENT;
  if (authored > 0) return authored * scale / 100;
  return lineHeight > 0 && scale > DEFAULT_PERCENT ? lineHeight * (scale - DEFAULT_PERCENT) / 100 : 0;
}
constexpr int gapPixels(const int lineHeight, const uint16_t percent) { return marginPixels(0, lineHeight, percent); }
}  // namespace paragraphSpacing
