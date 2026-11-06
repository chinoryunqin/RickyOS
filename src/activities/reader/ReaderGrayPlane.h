#pragma once

#include <cstdint>

namespace ReaderUtils {
// The caller owns a full physical plane; its B/W framebuffer remains intact.
template <typename Renderer, typename Paint>
void renderWholeGrayPlane(const Renderer& renderer, uint8_t* buffer, int height, Paint&& paint) {
  renderer.beginStripTarget(buffer, 0, height);
  renderer.clearScreen(0x00);
  paint();
  renderer.endStripTarget();
}
}  // namespace ReaderUtils
