#pragma once
#include <algorithm>
#include <cstdint>
namespace cameraPreview {
// Packed logical-coordinate bitmap: 1=black, row width must be divisible by 8.
struct Target {
  uint8_t* bits;
  int width, height, sourceWidth, sourceHeight;
};
inline void grayBlock(const Target& out, const uint8_t* pixels, int x, int y, int stride, int columns, int rows) {
  if (!out.bits || !pixels || out.width <= 0 || (out.width & 7) || out.height <= 0 || out.sourceWidth <= 0 ||
      out.sourceHeight <= 0 || stride <= 0 || columns <= 0 || rows <= 0)
    return;
  constexpr uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int row = 0; row < rows; ++row) {
    const int sy = y + row;
    if (sy < 0 || sy >= out.sourceHeight) continue;
    const int top = sy * out.height / out.sourceHeight;
    const int bottom = (sy + 1) * out.height / out.sourceHeight;
    for (int col = 0; col < std::min(stride, columns); ++col) {
      const int sx = x + col;
      if (sx < 0 || sx >= out.sourceWidth) continue;
      const int left = sx * out.width / out.sourceWidth;
      const int right = (sx + 1) * out.width / out.sourceWidth;
      const uint8_t gray = pixels[row * stride + col];
      for (int dy = top; dy < bottom; ++dy)
        for (int dx = left; dx < right; ++dx) {
          const unsigned offset = static_cast<unsigned>(dy * (out.width / 8) + dx / 8);
          const uint8_t mask = static_cast<uint8_t>(0x80 >> (dx & 7));
          if (gray < bayer[dy & 3][dx & 3] * 16 + 8)
            out.bits[offset] |= mask;
          else
            out.bits[offset] &= static_cast<uint8_t>(~mask);
        }
    }
  }
}
}  // namespace cameraPreview
