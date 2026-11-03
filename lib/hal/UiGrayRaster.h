#pragma once

#include <cstddef>
#include <cstdint>

// The UI raster is 4bpp, low nibble first; SSD1677's calibrated overlay has
// four physical tones. Quantize once without spatial dithering. Keep the panel
// capability at four: a richer working raster does not create a 16-gray LUT.
namespace uiGrayRaster {
enum class Plane { Base, Lsb, Msb };
constexpr uint8_t tone(uint8_t level) { return (level + 2) / 5; }
inline void row(const uint8_t* source, uint8_t* destination, size_t byteCount, Plane plane) {
  for (size_t byte = 0; byte < byteCount; ++byte) {
    uint8_t output = 0;
    for (unsigned pixel = 0; pixel < 8; ++pixel) {
      const size_t x = byte * 8 + pixel;
      const uint8_t level = (source[x / 2] >> ((x & 1) * 4)) & 15;
      const uint8_t value = tone(level);
      const bool bit = plane == Plane::Base ? value == 3 : plane == Plane::Lsb ? value == 1 : value == 1 || value == 2;
      if (bit) output |= 0x80 >> pixel;
    }
    destination[byte] = output;
  }
}
}  // namespace uiGrayRaster
