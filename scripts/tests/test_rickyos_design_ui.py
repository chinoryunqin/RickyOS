"""Execute native-tone helpers and inspect the real generated 1.2 font data."""
from pathlib import Path
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "src/components/RickyDesignUi.h"


class RickyDesignUiTest(unittest.TestCase):
    def test_legacy_ui_ids_are_registered_once_with_real_weight_families(self):
        source = (ROOT / "src/main.cpp").read_text()
        for font_id, family in [("UI_10_FONT_ID", "cjk12FontFamily"),
                                ("UI_12_FONT_ID", "ui16FallbackFamily"),
                                ("SMALL_FONT_ID", "designCaptionFamily")]:
            self.assertEqual(source.count(f"renderer.insertFont({font_id},"), 1)
            self.assertIn(f"renderer.insertFont({font_id}, {family});", source)
        self.assertIn("renderer.setFallbackFont(RickyDesignUi::BODY, kRtlFontId)", source)

    def test_whole_string_font_fallback_uses_actual_unicode_decoder(self):
        source = UI.read_text()
        run_cpp(r'''
#include <cassert>
#include <map>
#include "Utf8.cpp"
constexpr int BODY=1, TITLE=2;
struct Family {bool hasCodepoint(uint32_t cp) const {return cp<128 || cp==0x4e66;}};
struct GfxRenderer {
  std::map<int,Family> fonts{{TITLE,{}}};
  const auto& getFontMap() const {return fonts;}
};
''' + method(source, 'inline int fontFor(') + r'''
int main() {
  GfxRenderer renderer;
  assert(fontFor(renderer,TITLE,"Book")==TITLE);
  assert(fontFor(renderer,TITLE,"Book书")==TITLE);
  assert(fontFor(renderer,TITLE,"Book书猫")==BODY);
  assert(fontFor(renderer,TITLE,"é")==BODY);
  assert(fontFor(renderer,99,"Book")==BODY);
  assert(fontFor(renderer,BODY,"猫")==BODY);
}
''', include_dirs=(ROOT / "lib/Utf8",))

    def test_tone_fill_and_progress_have_no_checkerboard_or_extra_buffer(self):
        source = UI.read_text()
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
namespace ds=ricky::ds;
namespace fui {struct Rect {
  int16_t x,y,width,height;
  bool empty() const {return width<=0 || height<=0;}
};}
enum class Color {Black,White};
struct GfxRenderer {
  bool gray=false; mutable int writes=0, fills=0;
  mutable int pixels[64][64]{};
  bool isGrayscale16Active() const {return gray;}
  void drawGrayscale16Pixel(int x,int y,int value) const {
    assert(x>=0 && x<64 && y>=0 && y<64);
    assert(value%17==0); pixels[y][x]=value; ++writes;
  }
  void fillRoundedRect(int x,int y,int w,int h,int,Color color) const {
    ++fills;
    for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col)
      pixels[row][col]=color==Color::Black?0:255;
  }
};
''' + method(source, 'inline void fill(') + method(source, 'inline void progress(') + r'''
int main() {
  const ds::gray::Level tones[]={ds::gray::kInk,ds::gray::kInk2,ds::gray::kMute,
                                ds::gray::kLine,ds::gray::kTint,ds::gray::kPaper};
  for(auto tone:tones) {
    GfxRenderer renderer;
    fill(renderer,{8,8,24,32},tone,12);
    assert(renderer.writes==0 && renderer.fills==1);
    assert(renderer.pixels[16][16]==(ds::gray::oneBitInk(tone)?0:255));
    renderer.gray=true;
    fill(renderer,{8,8,24,32},tone,12);
    assert(renderer.pixels[16][16]==ds::gray::toRgb8(tone));
    assert(renderer.pixels[0][0]==0); // No writes beyond the rectangle.
  }
  for(int percent:{-20,0,37,100,150}) {
    GfxRenderer renderer;
    progress(renderer,{8,8,40,8},percent);
    assert(renderer.writes==0);
    renderer.gray=true;
    progress(renderer,{8,8,40,8},percent);
    assert(renderer.pixels[12][27]==(percent>=50?0:221));
  }
}
''')

    def test_real_font_line_boxes_and_metrics(self):
        for path in (ROOT / 'lib/EpdFont/builtinFonts').glob('ricky_*.h'):
            for line in path.read_bytes().splitlines():
                self.assertEqual(line, line.rstrip(), str(path))
        run_cpp(r'''
#include <cassert>
#pragma clang diagnostic push
// EpdFontData appends optional runtime handlers; built-in positional tables
// deliberately zero-initialize them. Verify that contract below.
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "ricky_caption_10_regular.h"
#include "ricky_headline_14_medium.h"
#include "ricky_title_16_bold.h"
#include "ricky_display_23_bold.h"
#pragma clang diagnostic pop
int main() {
  const EpdFontData* fonts[]={&ricky_caption_10_regular,&ricky_headline_14_medium,
                            &ricky_title_16_bold,&ricky_display_23_bold};
  const int heights[]={28,40,44,56}, baselines[]={22,32,34,47};
  for(int i=0;i<4;++i) {
    const auto& font=*fonts[i];
    assert(font.advanceY==heights[i] && font.ascender==baselines[i]);
    assert(font.descender==baselines[i]-heights[i] && !font.is2Bit);
    assert(font.groups==nullptr && font.groupCount==0);
    assert(font.glyphMissHandler==nullptr && font.glyphMissCtx==nullptr);
    assert(font.vectorBitmapHandler==nullptr && font.kernHandler==nullptr && font.is4Bit);
    unsigned tones=0;
    bool oddGlyph=false;
    for(uint32_t j=0;j<font.intervalCount;++j) {
      const auto& interval=font.intervals[j];
      for(uint32_t cp=interval.first;cp<=interval.last;++cp) {
        const auto& glyph=font.glyph[interval.offset+cp-interval.first];
        assert(font.ascender-glyph.top>=0);
        assert(font.ascender-glyph.top+glyph.height<=font.advanceY);
        const unsigned pixels=unsigned(glyph.width)*glyph.height;
        assert(glyph.dataLength==(pixels+1)/2); // no per-row padding
        const auto* bitmap=font.bitmap+glyph.dataOffset;
        for(unsigned p=0;p<pixels;++p)
          tones|=1u<<((bitmap[p/2]>>(p%2==0?4:0))&15);
        if(pixels%2) {oddGlyph=true;assert((bitmap[pixels/2]&15)==0);}
      }
    }
    assert(oddGlyph && tones==65535);
  }
}
''', include_dirs=(ROOT / "lib/EpdFont", ROOT / "lib/EpdFont/builtinFonts"))

    def test_six_default_cover_templates_are_stable_and_not_path_based(self):
        run_cpp(r'''
#include <cassert>
#include <cstdio>
int main() {
  unsigned seen=0;
  for(int i=0;i<1000;++i) {
    char title[32];std::snprintf(title,sizeof(title),"Book %d",i);
    auto style=ricky::ds::component::coverTemplateFor(title);
    assert(style<6 && style==ricky::ds::component::coverTemplateFor(title));
    seen|=1u<<style;
  }
  assert(seen==63);
}
''')

    def test_isolated_dog_asset_preserves_native_coverage_without_ring(self):
        run_cpp(r'''
#include <cassert>
#include "images/RickyDog.h"
int main() {
  assert(RickyDogAsset::width==150 && RickyDogAsset::height==100);
  assert(sizeof(RickyDogAsset::coverage)==7500);
  unsigned seen=0;
  for(auto byte:RickyDogAsset::coverage) {seen|=1u<<(byte>>4);seen|=1u<<(byte&15);}
  assert(seen==65535);
}
''', include_dirs=(ROOT / "src",))


if __name__ == "__main__":
    unittest.main()
