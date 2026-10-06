#ifdef RICKYOS_PRODUCT
#include "RickyClockDigits.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "fonts/rickyClockDigits.h"

namespace RickyClockDigits {
namespace {
const ricky_clock_digits::Glyph* glyphFor(const char ch) {
  for (const auto& glyph : ricky_clock_digits::kGlyphs) {
    if (glyph.ch == ch) return &glyph;
  }
  return nullptr;
}

int cellWidth(const ricky_clock_digits::Glyph& glyph) {
  return glyph.ch == ':' ? glyph.advance : ricky_clock_digits::kCellWidth;
}

uint8_t coverageAt(const ricky_clock_digits::Glyph& glyph, const int x, const int y) {
  const uint8_t packed = glyph.bits[y * ((glyph.width + 1) / 2) + x / 2];
  return (x & 1) ? (packed & 0x0F) : (packed >> 4);
}
}  // namespace

int capHeight() { return ricky_clock_digits::kCapHeight; }

int width(const char* text) {
  int total = 0;
  for (const char* c = text; *c; ++c) {
    if (const auto* glyph = glyphFor(*c)) total += cellWidth(*glyph);
  }
  return total;
}

void draw(GfxRenderer& renderer, int x, const int yTop, const char* text) {
  for (const char* c = text; *c; ++c) {
    const auto* glyph = glyphFor(*c);
    if (!glyph) continue;
    const int originX = x + (cellWidth(*glyph) - glyph->advance) / 2 + glyph->left;
    // Column order: in portrait a logical column is a panel row (see GfxRenderer).
    for (int gx = 0; gx < glyph->width; ++gx) {
      for (int gy = 0; gy < glyph->height; ++gy) {
        const uint8_t ink = coverageAt(*glyph, gx, gy);
        if (ink == 0) continue;
        const int px = originX + gx;
        const int py = yTop + gy;
        const int paper = renderer.grayscale16Level(px, py);
        const int level = paper - (paper * ink + 7) / 15;  // ink over the picture, toward black
        renderer.drawGrayscale16Pixel(px, py, static_cast<uint8_t>(level * 17));
      }
    }
    x += cellWidth(*glyph);
  }
}

void fadeToPaper(GfxRenderer& renderer, const int left, const int top, const int width, const int fullAt,
                 const int bottom, const int strength) {
  if (bottom <= top || width <= 0) return;
  const int ramp = std::max(1, fullAt - top);
  for (int x = left; x < left + width; ++x) {
    for (int y = top; y < bottom; ++y) {
      // 0..strength of the way from the picture to paper; an ordered dither on the
      // fractional step keeps 16 levels from banding across a tall fade.
      const int alpha256 = y >= fullAt ? strength * 256 / 15 : (y - top) * strength * 256 / (15 * ramp);
      const int level = renderer.grayscale16Level(x, y);
      const int lifted = level * 256 + (15 - level) * alpha256;
      static constexpr uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
      const int threshold = kBayer4[y & 3][x & 3] * 16 + 8;
      const int out = std::min(15, lifted / 256 + ((lifted & 255) > threshold ? 1 : 0));
      if (out != level) renderer.drawGrayscale16Pixel(x, y, static_cast<uint8_t>(out * 17));
    }
  }
}

}  // namespace RickyClockDigits
#endif
