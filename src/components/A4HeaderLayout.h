#pragma once

#include <algorithm>

// Original header chrome and the larger invisible Back target share a layout.
namespace A4HeaderLayout {
inline constexpr int statusHeight = 24;
inline constexpr int visualHeaderHeight = 66;
inline constexpr int targetHeight = 72;
inline constexpr int contentGap = 12;
inline constexpr int headerHeight = statusHeight + targetHeight + contentGap;

struct Target {
  int x;
  int y;
  int width;
  int height;
};
inline Target backTarget(int x, int y, int width, int height) {
  const int left = std::min(0, x);
  const int right = x + std::max(0, width / 2);
  return {left, y + statusHeight, width > 0 ? std::max(0, right - left) : 0,
          std::min(targetHeight, std::max(0, height - statusHeight - contentGap))};
}
}  // namespace A4HeaderLayout
