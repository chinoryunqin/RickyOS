#include "../firmware/rickyos-plus-1.2/upstream/lib/EpdFont/EpdFontData.h"
#include <uzlib.h>
#include <FontDecompressor.h>
#include <Utf8.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
namespace baseline {
#include <builtinFonts/notosans_cjk_12_rickyos.h>
#include <builtinFonts/notosans_cjk_14.h>
#include <builtinFonts/notosans_cjk_16.h>
#include <builtinFonts/ricky_title_16_bold.h>
#include <builtinFonts/ricky_headline_14_medium.h>
#include <builtinFonts/ricky_display_23_bold.h>
#include <builtinFonts/ricky_caption_10_regular.h>
}
namespace packed {
#include <fonts/notosans_cjk_12_rickyos.packed.h>
#include <fonts/notosans_cjk_14.packed.h>
#include <fonts/notosans_cjk_16.packed.h>
#include <fonts/ricky_title_16_bold.packed.h>
#include <fonts/ricky_headline_14_medium.packed.h>
#include <fonts/ricky_display_23_bold.packed.h>
#include <fonts/ricky_caption_10_regular.packed.h>
}
static void verify(const EpdFontData& original, const EpdFontData& compressed) {
  assert(original.is4Bit == compressed.is4Bit && original.is2Bit == compressed.is2Bit);
  const unsigned bits = original.is4Bit ? 4 : 2;
  const unsigned perByte = 8 / bits;
  const unsigned mask = (1u << bits) - 1;
  assert(original.intervalCount == compressed.intervalCount);
  assert(original.advanceY == compressed.advanceY && original.ascender == compressed.ascender && original.descender == compressed.descender);
  assert(memcmp(original.intervals, compressed.intervals, original.intervalCount * sizeof(EpdUnicodeInterval)) == 0);
  FontDecompressor decoder;
  assert(decoder.init());
  size_t glyphCount = 0;
  for (size_t g = 0; g < compressed.groupCount; ++g) {
    const auto& group = compressed.groups[g];
    assert(group.firstGlyphIndex == glyphCount && group.uncompressedSize <= 16384);
    std::vector<uint8_t> decoded(group.uncompressedSize + 2, 0xa5);
    uzlib_uncomp stream{};
    uzlib_uncompress_init(&stream, nullptr, 0);
    stream.source = compressed.bitmap + group.compressedOffset;
    stream.source_limit = stream.source + group.compressedSize;
    stream.dest_start = decoded.data() + 1;
    stream.dest = stream.dest_start;
    stream.dest_limit = stream.dest + group.uncompressedSize;
    assert(uzlib_uncompress(&stream) >= 0 && stream.dest == stream.dest_limit);
    assert(decoded.front() == 0xa5 && decoded.back() == 0xa5);
    size_t offset = 0;
    for (size_t i = group.firstGlyphIndex; i < group.firstGlyphIndex + group.glyphCount; ++i) {
      const auto& a = original.glyph[i]; const auto& b = compressed.glyph[i];
      assert(a.width == b.width && a.height == b.height && a.advanceX == b.advanceX && a.left == b.left && a.top == b.top && a.dataLength == b.dataLength);
      const size_t stride = (a.width + perByte - 1) / perByte;
      for (size_t y = 0; y < a.height; ++y) for (size_t x = 0; x < a.width; ++x) {
        const size_t index = y * a.width + x;
        const unsigned expected = (original.bitmap[a.dataOffset + index / perByte] >> ((perByte - 1 - index % perByte) * bits)) & mask;
        const unsigned actual = (decoded[1 + offset + y * stride + x / perByte] >> ((perByte - 1 - x % perByte) * bits)) & mask;
        assert(expected == actual);
      }
      const uint8_t* actualBitmap = decoder.getBitmap(&compressed, &b, i);
      if (a.dataLength) {
        assert(actualBitmap);
        assert(memcmp(actualBitmap, original.bitmap + a.dataOffset, a.dataLength) == 0);
      }
      offset += stride * a.height;
    }
    assert(offset == group.uncompressedSize);
    glyphCount += group.glyphCount;
  }
  assert(glyphCount == original.intervals[original.intervalCount - 1].offset + original.intervals[original.intervalCount - 1].last - original.intervals[original.intervalCount - 1].first + 1);
  // Exercise the page prewarm path too, including odd-width glyphs.
  std::string batch;
  size_t checked = 0;
  for (size_t interval = 0; interval < original.intervalCount && checked < 256; ++interval) {
    const auto& range = original.intervals[interval];
    for (uint32_t cp = range.first; cp <= range.last && checked < 256; ++cp, ++checked)
      utf8AppendCodepoint(cp, batch);
  }
  assert(decoder.prewarmCache(&compressed, batch.c_str()) == 0);
  checked = 0;
  for (size_t interval = 0; interval < original.intervalCount && checked < 256; ++interval) {
    const auto& range = original.intervals[interval];
    for (uint32_t cp = range.first; cp <= range.last && checked < 256; ++cp, ++checked) {
      size_t i = range.offset + cp - range.first;
      const auto& a = original.glyph[i];
      const uint8_t* bitmap = decoder.getBitmap(&compressed, &compressed.glyph[i], i);
      if (a.dataLength) assert(bitmap && memcmp(bitmap, original.bitmap + a.dataOffset, a.dataLength) == 0);
    }
  }
  printf("%zu glyphs: firmware uzlib decoder, pixel/metric/Unicode preservation and buffer guards passed\n", glyphCount);
}
// Faces the design already packs (frequency-ordered groups) are copied unchanged, so the
// source is no raw baseline: check the copy matches and every glyph decodes through the
// firmware decoder twice with identical bytes (the design's FontDecompressorTest checks pixels).
static void verifyUpstreamPacked(const EpdFontData& source, const EpdFontData& copy) {
  assert(source.groupCount > 0 && source.groupCount == copy.groupCount);
  assert(memcmp(source.groups, copy.groups, source.groupCount * sizeof(EpdFontGroup)) == 0);
  const auto& lastGroup = source.groups[source.groupCount - 1];
  assert(memcmp(source.bitmap, copy.bitmap, lastGroup.compressedOffset + lastGroup.compressedSize) == 0);
  const auto& lastInterval = source.intervals[source.intervalCount - 1];
  const size_t glyphCount = lastInterval.offset + lastInterval.last - lastInterval.first + 1;
  assert(memcmp(source.glyph, copy.glyph, glyphCount * sizeof(EpdGlyph)) == 0);
  FontDecompressor forward, backward;
  assert(forward.init() && backward.init());
  std::vector<std::vector<uint8_t>> decoded(glyphCount);
  for (size_t i = 0; i < glyphCount; ++i) {
    const auto& glyph = copy.glyph[i];
    if (!glyph.dataLength) continue;
    const uint8_t* bitmap = forward.getBitmap(&copy, &glyph, i);
    assert(bitmap);
    decoded[i].assign(bitmap, bitmap + glyph.dataLength);
  }
  for (size_t i = glyphCount; i-- > 0;) {
    const auto& glyph = copy.glyph[i];
    if (!glyph.dataLength) continue;
    const uint8_t* bitmap = backward.getBitmap(&copy, &glyph, i);
    assert(bitmap && memcmp(decoded[i].data(), bitmap, glyph.dataLength) == 0);
  }
  printf("%zu glyphs: upstream-packed copy identical and decodable\n", glyphCount);
}
int main() {
  uzlib_init();
  verifyUpstreamPacked(baseline::notosans_cjk_12_rickyos, packed::notosans_cjk_12_rickyos);
  verify(baseline::notosans_cjk_14, packed::notosans_cjk_14);
  verify(baseline::notosans_cjk_16, packed::notosans_cjk_16);
  verify(baseline::ricky_title_16_bold, packed::ricky_title_16_bold);
  verify(baseline::ricky_headline_14_medium, packed::ricky_headline_14_medium);
  verify(baseline::ricky_display_23_bold, packed::ricky_display_23_bold);
  verify(baseline::ricky_caption_10_regular, packed::ricky_caption_10_regular);
}
