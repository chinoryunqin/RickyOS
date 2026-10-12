#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace freeink::plus {
// Native gray begin starts a complete new page, rather than an overlay on
// the previous monochrome frame. Reuse the existing panel allocation.
inline void clearGrayscale(uint8_t* frame, size_t bytes) {
  memset(frame, 0xff, bytes);
}
// Native scan order is unchanged. The SDK supplies MSB-first 1bpp (white=1);
// EPDiy consumes even pixels in the low nibble, with white=15. Caller owns
// both buffers; the destination must have four times sourceBytes storage.
inline void expandMonochrome(const uint8_t* source, size_t sourceBytes, uint8_t* destination) {
  for (size_t i = 0; i < sourceBytes; ++i) {
    const uint8_t value = source[i];
    for (unsigned pair = 0; pair < 4; ++pair) {
      const uint8_t low = value & (0x80u >> (pair * 2)) ? 15 : 0;
      const uint8_t high = value & (0x40u >> (pair * 2)) ? 0xf0 : 0;
      destination[i * 4 + pair] = low | high;
    }
  }
}
}
