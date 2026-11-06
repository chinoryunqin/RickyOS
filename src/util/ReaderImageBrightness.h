#pragma once
#include <algorithm>

// Reader UI calibration only. Decoder callers still pass physical offsets.
namespace readerImageBrightness {
inline constexpr int kScaleVersion = 2;
inline constexpr int kZeroOffset = -15;
inline constexpr int kOriginal = -kZeroOffset;
inline constexpr int kMinimum = -40;
inline constexpr int kMaximum = 40;
inline constexpr int toRender(int value) { return std::clamp(value, kMinimum, kMaximum) + kZeroOffset; }
inline constexpr int fromLegacy(int value) {
  value = std::clamp(value, kMinimum, kMaximum);
  // A saved neutral trial value follows the new product baseline. Explicit
  // non-neutral preferences retain their physical offset where the UI fits it.
  return value == 0 ? 0 : std::clamp(value - kZeroOffset, kMinimum, kMaximum);
}
}  // namespace readerImageBrightness
