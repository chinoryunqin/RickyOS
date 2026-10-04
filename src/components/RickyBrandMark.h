#pragma once

#include <algorithm>
#include <cstdint>

#include "Rect.h"
#include "images/RickyLogo.h"

// User-selected portrait + dog + circular border + RickyOS wordmark.
// Immutable Flash pixels; reuse the existing framebuffer with no heap allocation.
namespace RickyBrandMark {
constexpr Rect fit(const Rect box) {
  const int height = std::max(
      0, std::min({box.height, RickyLogoAsset::height, box.width * RickyLogoAsset::height / RickyLogoAsset::width}));
  const int width = height * RickyLogoAsset::width / RickyLogoAsset::height;
  return Rect{box.x + (box.width - width) / 2, box.y + (box.height - height) / 2, width, height};
}

constexpr bool pixel(const int x, const int y, const uint8_t phase) {
  if (x < 0 || y < 0 || x >= RickyLogoAsset::width || y >= RickyLogoAsset::height) return false;
  const int index = y * (RickyLogoAsset::width / 8) + x / 8;
  const uint8_t mask = 0x80U >> (x % 8);
  if (phase == 0) return (RickyLogoAsset::circleBits[index] & mask) == 0;
  if ((RickyLogoAsset::bits[index] & mask) != 0) return false;
  if (phase >= 3 || (RickyLogoAsset::circleBits[index] & mask) == 0) return true;
  return y < RickyLogoAsset::wordmarkTop && (phase >= 2 || x < RickyLogoAsset::personRight);
}

template <typename Renderer>
void draw(const Renderer& renderer, const Rect box, const uint8_t phase) {
  const Rect mark = fit(box);
  if (mark.width <= 0 || mark.height <= 0) return;
  for (int y = 0; y < mark.height; ++y) {
    const int sourceY = y * RickyLogoAsset::height / mark.height;
    for (int x = 0; x < mark.width; ++x) {
      if (pixel(x * RickyLogoAsset::width / mark.width, sourceY, phase)) {
        renderer.drawPixel(mark.x + x, mark.y + y, true);
      }
    }
  }
}
}  // namespace RickyBrandMark
