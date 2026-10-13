#include <gtest/gtest.h>
#include <zlib.h>

#include <cstring>
#include <random>
#include <vector>

#include "FontDecompressor.h"

// Raw DEFLATE groups carry no checksum; uzlib still links the hooks.
extern "C" uint32_t uzlib_adler32(const void*, unsigned int, const uint32_t previous) { return previous; }
extern "C" uint32_t uzlib_crc32(const void*, unsigned int, const uint32_t previous) { return previous; }

namespace {

// A synthetic packed font: kGroups contiguous groups of kPerGroup glyphs, each group a
// raw-DEFLATE stream of byte-aligned 2-bit rows, the layout fontconvert and
// compress_plus_builtin_fonts.py produce.
constexpr int kGlyphs = 44;
constexpr int kPerGroup = 4;
constexpr int kGroups = kGlyphs / kPerGroup;  // 11 > the 8 hot slots

struct Font {
  std::vector<EpdGlyph> glyphs;
  std::vector<EpdFontGroup> groups;
  std::vector<uint8_t> data;
  std::vector<std::vector<uint8_t>> pixels;  // per glyph, width*height 2-bit values
  EpdFontData font{};
};

uint8_t pixelValue(int glyph, int x, int y) { return static_cast<uint8_t>((glyph * 7 + x * 3 + y * 5) & 3); }

std::vector<uint8_t> deflateRaw(const std::vector<uint8_t>& in) {
  z_stream stream{};
  EXPECT_EQ(deflateInit2(&stream, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY), Z_OK);
  std::vector<uint8_t> out(compressBound(static_cast<uLong>(in.size())) + 64);
  stream.next_in = const_cast<Bytef*>(in.data());
  stream.avail_in = static_cast<uInt>(in.size());
  stream.next_out = out.data();
  stream.avail_out = static_cast<uInt>(out.size());
  EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
  out.resize(stream.total_out);
  deflateEnd(&stream);
  return out;
}

Font& testFont() {
  static Font f = [] {
    Font font;
    for (int group = 0; group < kGroups; ++group) {
      std::vector<uint8_t> aligned;
      for (int i = 0; i < kPerGroup; ++i) {
        const int index = group * kPerGroup + i;
        EpdGlyph glyph{};
        glyph.width = static_cast<uint8_t>(3 + index % 7);  // widths 3..9: odd and multiple-of-4 rows
        glyph.height = static_cast<uint8_t>(4 + index % 3);
        glyph.dataLength = static_cast<uint16_t>((glyph.width * glyph.height * 2 + 7) / 8);
        std::vector<uint8_t> px;
        const int stride = (glyph.width + 3) / 4;
        for (int y = 0; y < glyph.height; ++y) {
          std::vector<uint8_t> row(stride, 0);
          for (int x = 0; x < glyph.width; ++x) {
            const uint8_t v = pixelValue(index, x, y);
            px.push_back(v);
            row[x / 4] |= static_cast<uint8_t>(v << ((3 - x % 4) * 2));
          }
          aligned.insert(aligned.end(), row.begin(), row.end());
        }
        font.pixels.push_back(px);
        font.glyphs.push_back(glyph);
      }
      const auto packed = deflateRaw(aligned);
      EpdFontGroup info{};
      info.compressedOffset = static_cast<uint32_t>(font.data.size());
      info.compressedSize = static_cast<uint32_t>(packed.size());
      info.uncompressedSize = static_cast<uint32_t>(aligned.size());
      info.glyphCount = kPerGroup;
      info.firstGlyphIndex = static_cast<uint32_t>(group * kPerGroup);
      font.groups.push_back(info);
      font.data.insert(font.data.end(), packed.begin(), packed.end());
    }
    font.font.bitmap = font.data.data();
    font.font.glyph = font.glyphs.data();
    font.font.groups = font.groups.data();
    font.font.groupCount = kGroups;
    font.font.is2Bit = true;
    return font;
  }();
  return f;
}

// The packed 2-bit bitmap compactSingleGlyph() promises: rows run on without padding.
std::vector<uint8_t> expectedPacked(int index) {
  const auto& glyph = testFont().glyphs[index];
  std::vector<uint8_t> out((glyph.width * glyph.height * 2 + 7) / 8, 0);
  int bit = 0;
  for (const uint8_t v : testFont().pixels[index]) {
    out[bit / 8] |= static_cast<uint8_t>(v << (6 - bit % 8));
    bit += 2;
  }
  return out;
}

void expectGlyph(FontDecompressor& decompressor, int index) {
  Font& font = testFont();
  const uint8_t* bitmap = decompressor.getBitmap(&font.font, &font.glyphs[index], index);
  ASSERT_NE(bitmap, nullptr) << "glyph " << index;
  const auto expected = expectedPacked(index);
  EXPECT_EQ(0, memcmp(bitmap, expected.data(), expected.size())) << "glyph " << index;
}

TEST(FontDecompressorTest, EveryGlyphOfEveryGroupDecodes) {
  FontDecompressor decompressor;
  ASSERT_TRUE(decompressor.init());
  for (int i = 0; i < kGlyphs; ++i) expectGlyph(decompressor, i);
  for (int i = kGlyphs; i-- > 0;) expectGlyph(decompressor, i);
}

TEST(FontDecompressorTest, AlternatingGroupsStayHot) {
  FontDecompressor decompressor;
  ASSERT_TRUE(decompressor.init());
  // Text mixing glyphs from three groups used to re-inflate a group on every change.
  for (int round = 0; round < 20; ++round)
    for (const int index : {1, 9, 22, 2, 10, 23}) expectGlyph(decompressor, index);
  EXPECT_EQ(decompressor.getStats().cacheMisses, 3u);
}

TEST(FontDecompressorTest, LeastRecentlyUsedGroupIsEvicted) {
  FontDecompressor decompressor;
  ASSERT_TRUE(decompressor.init());
  for (int group = 0; group < 8; ++group) expectGlyph(decompressor, group * kPerGroup);
  EXPECT_EQ(decompressor.getStats().cacheMisses, 8u);
  expectGlyph(decompressor, 0);              // group 0 is now the most recent
  expectGlyph(decompressor, 8 * kPerGroup);  // evicts group 1, the least recent
  EXPECT_EQ(decompressor.getStats().cacheMisses, 9u);
  expectGlyph(decompressor, 1);  // group 0 still hot
  EXPECT_EQ(decompressor.getStats().cacheMisses, 9u);
  expectGlyph(decompressor, kPerGroup);  // group 1 was evicted
  EXPECT_EQ(decompressor.getStats().cacheMisses, 10u);
}

TEST(FontDecompressorTest, RandomAccessMatchesAcrossEvictions) {
  FontDecompressor decompressor;
  ASSERT_TRUE(decompressor.init());
  std::mt19937 random(42);
  for (int i = 0; i < 2000; ++i) expectGlyph(decompressor, static_cast<int>(random() % kGlyphs));
  decompressor.clearCache();
  expectGlyph(decompressor, 5);
}

}  // namespace
