#include "../firmware/rickyos-plus-1.2/components/rickyos_display/include/PlusFramebuffer.h"
#include "../firmware/rickyos-plus-1.2/components/rickyos_platform/include/PlusCoverKeys.h"
#include "../firmware/rickyos-plus-1.2/components/rickyos_platform/include/PlusBattery.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

static uint8_t pixel(const uint8_t* buffer, size_t index) {
  return (buffer[index / 2] >> ((index & 1) * 4)) & 15;
}
int main() {
  uint16_t percent = 777;
  assert(!plus_board::batteryPercentFromMv(0, percent) && percent == 777);
  assert(!plus_board::batteryPercentFromMv(5001, percent) && percent == 777);
  assert(plus_board::batteryPercentFromMv(3300, percent) && percent == 0);
  assert(plus_board::batteryPercentFromMv(3815, percent) && percent == 50);
  assert(plus_board::batteryPercentFromMv(4330, percent) && percent == 100);
  uint16_t previous = 0;
  for (uint16_t mv = 2500; mv <= 5000; ++mv) {
    assert(plus_board::batteryPercentFromMv(mv, percent));
    assert(percent <= 100 && percent >= previous);
    previous = percent;
  }
  using plus_board::CoverKey;
  using plus_board::coverKeyAt;
  assert(coverKeyAt(100, 1600) == CoverKey::Home);
  assert(coverKeyAt(200, 1600) == CoverKey::Previous);
  assert(coverKeyAt(300, 1600) == CoverKey::Next);
  for (uint16_t y : {uint16_t(0), uint16_t(1215), uint16_t(1569), uint16_t(1631)})
    for (uint16_t x : {uint16_t(100), uint16_t(200), uint16_t(300)})
      assert(coverKeyAt(x, y) == CoverKey::None);
  assert(coverKeyAt(70, 1570) == CoverKey::Home);
  assert(coverKeyAt(130, 1630) == CoverKey::Home);
  assert(coverKeyAt(150, 1600) == CoverKey::None);
  assert(coverKeyAt(250, 1600) == CoverKey::None);
  for (unsigned bits = 0; bits < 256; ++bits) {
    const uint8_t source = static_cast<uint8_t>(bits);
    std::array<uint8_t, 6> guarded{0xa5, 0, 0, 0, 0, 0x5a};
    freeink::plus::expandMonochrome(&source, 1, guarded.data() + 1);
    assert(guarded.front() == 0xa5 && guarded.back() == 0x5a);
    for (size_t x = 0; x < 8; ++x)
      assert(pixel(guarded.data() + 1, x) == ((bits >> (7 - x)) & 1 ? 15 : 0));
  }
  // Detect reversed scan order and row-boundary corruption at the actual panel size.
  constexpr size_t width = 1216, height = 684, bytes = width * height / 8;
  std::vector<uint8_t> source(bytes, 0xff), destination(bytes * 4 + 2, 0xa5);
  const std::array<size_t, 5> markers{0, width - 1, width, width * (height - 1), width * height - 1};
  for (auto index : markers) source[index / 8] &= ~(0x80u >> (index & 7));
  freeink::plus::expandMonochrome(source.data(), bytes, destination.data() + 1);
  assert(destination.front() == 0xa5 && destination.back() == 0xa5);
  for (size_t index = 0; index < width * height; ++index) {
    bool black = false;
    for (auto marker : markers) if (index == marker) black = true;
    assert(pixel(destination.data() + 1, index) == (black ? 0 : 15));
  }
  // Start a gray reader page after the monochrome home screen. All old ink
  // must disappear from RAM before drawing the new page, including its margins.
  freeink::plus::clearGrayscale(destination.data() + 1, bytes * 4);
  assert(destination.front() == 0xa5 && destination.back() == 0xa5);
  for (size_t index = 0; index < width * height; ++index)
    assert(pixel(destination.data() + 1, index) == 15);
  destination[1 + width / 2] = 0x70; // New reader ink only.
  assert(pixel(destination.data() + 1, width) == 0);
  assert(pixel(destination.data() + 1, width + 1) == 7);
  for (auto marker : markers) if (marker != width)
    assert(pixel(destination.data() + 1, marker) == 15);
  puts("Plus framebuffer: scan, guards and home-to-gray-page clearing passed");
}
