#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Uniform scaling and its inverse share one rectangle. No scene reflow, no
// dither, and no image-sized scratch allocation.
namespace uiSceneFit {
struct Rect {
  int x, y, width, height;
};
inline Rect fit(Rect source, Rect bounds) {
  if (source.width <= 0 || source.height <= 0 || bounds.width <= 0 || bounds.height <= 0) return {};
  int width = bounds.width, height = static_cast<int>(int64_t(width) * source.height / source.width);
  if (height > bounds.height) {
    height = bounds.height;
    width = static_cast<int>(int64_t(height) * source.width / source.height);
  }
  return {bounds.x + (bounds.width - width) / 2, bounds.y + (bounds.height - height) / 2, width, height};
}
inline bool inverse(Rect source, Rect target, int& x, int& y) {
  if (target.width <= 0 || target.height <= 0 || x < target.x || y < target.y || x >= target.x + target.width ||
      y >= target.y + target.height)
    return false;
  x = source.x + static_cast<int>(int64_t(x - target.x) * source.width / target.width);
  y = source.y + static_cast<int>(int64_t(y - target.y) * source.height / target.height);
  return true;
}
inline uint8_t portraitPixel(const uint8_t* frame, int nativeWidth, int nativeHeight, int x, int y) {
  if (!frame || x < 0 || y < 0 || x >= nativeHeight || y >= nativeWidth) return 15;
  const size_t index = static_cast<size_t>(nativeHeight - 1 - x) * (nativeWidth / 2) + y / 2;
  return (frame[index] >> ((y & 1) * 4)) & 15;
}
// Exact area average in integer source/target units. The normal downscale is
// about 0.63x, so one destination pixel visits at most 3x3 source pixels.
inline uint8_t sample(const uint8_t* frame, int nativeWidth, int nativeHeight, Rect source, Rect target, int x, int y) {
  if (!frame || source.width <= 0 || source.height <= 0 || target.width <= 0 || target.height <= 0 || x < 0 || y < 0 ||
      x >= target.width || y >= target.height)
    return 15;
  const int left = x * source.width, right = (x + 1) * source.width;
  const int top = y * source.height, bottom = (y + 1) * source.height;
  int64_t sum = 0;
  for (int sy = top / target.height; sy < (bottom + target.height - 1) / target.height; ++sy) {
    const int wy = std::min(bottom, (sy + 1) * target.height) - std::max(top, sy * target.height);
    for (int sx = left / target.width; sx < (right + target.width - 1) / target.width; ++sx) {
      const int wx = std::min(right, (sx + 1) * target.width) - std::max(left, sx * target.width);
      sum += int64_t(wx) * wy * portraitPixel(frame, nativeWidth, nativeHeight, source.x + sx, source.y + sy);
    }
  }
  const int64_t area = int64_t(source.width) * source.height;
  return static_cast<uint8_t>((sum + area / 2) / area);
}
}  // namespace uiSceneFit
