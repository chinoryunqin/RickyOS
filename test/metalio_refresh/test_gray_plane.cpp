#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

#include "ReaderGrayPlane.h"

namespace {
constexpr int kStride = 100;
constexpr int kHeight = 480;
constexpr int kBytes = kStride * kHeight;
struct Renderer {
  mutable uint8_t* target = nullptr;
  mutable int firstRow = 0;
  mutable int rows = 0;
  std::array<uint8_t, kBytes> bw{};
  void beginStripTarget(uint8_t* p, int y, int count) const {
    assert(!target && y >= 0 && count > 0 && y + count <= kHeight);
    target = p;
    firstRow = y;
    rows = count;
  }
  void clearScreen(uint8_t value) const { std::fill_n(target, rows * kStride, value); }
  void endStripTarget() const { target = nullptr; }
  void pixel(int x, int y) const {
    if (y >= firstRow && y < firstRow + rows) target[(y - firstRow) * kStride + x / 8] |= 0x80 >> (x % 8);
  }
};
}  // namespace

int main() {
  Renderer renderer;
  renderer.bw.fill(0xa5);
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int plane = 0; plane < 2; ++plane) {
      std::array<uint8_t, kBytes + 2> whole{}, strips{};
      whole.fill(0x7b);
      strips.fill(0x7b);
      int paints = 0;
      auto paint = [&] {
        ++paints;
        for (int y = 0; y < kHeight; ++y)
          for (int x = 0; x < kStride * 8; ++x) {
            const int px = orientation & 1 ? y : x;
            const int py = orientation & 1 ? x : y;
            const int level = (px / 7 + py / 11 + orientation) & 3;
            if (level & (1 << plane)) renderer.pixel(x, y);
          }
      };
      ReaderUtils::renderWholeGrayPlane(renderer, whole.data() + 1, kHeight, paint);
      assert(paints == 1 && renderer.target == nullptr);
      paints = 0;
      for (int y = 0; y < kHeight; y += 80) {
        renderer.beginStripTarget(strips.data() + 1 + y * kStride, y, std::min(80, kHeight - y));
        renderer.clearScreen(0);
        paint();
        renderer.endStripTarget();
      }
      assert(paints == 6 && whole == strips);
      assert(whole.front() == 0x7b && whole.back() == 0x7b);
      for (auto byte : renderer.bw) assert(byte == 0xa5);
    }
  }
}
