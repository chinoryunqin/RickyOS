"""Statistics geometry, calendar routing and nested text clipping at large UI sizes."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp
ROOT = Path(__file__).resolve().parents[2]

class StatsLayoutTest(unittest.TestCase):
    def test_every_day_and_gap_in_full_months_and_scrolled_viewports(self):
        run_cpp(r'''
#include <cassert>
#include <initializer_list>
#include "components/RickyStatsLayout.h"
int main() {
  using namespace RickyStatsLayout;
  for(int width:{360,592,684,1120}) for(int line:{24,36,52,80})
  for(int leading=0;leading<7;++leading) for(unsigned days:{28,29,30,31}) {
    const int row=calendarRowHeight(line,16), weeks=(leading+days+6)/7;
    for(int offset:{0,80,400,1000}) {
      Rect grid{40,300-offset,width/7*7,row*weeks};
      for(unsigned day=1;day<=days;++day) {
        const auto cell=calendarCell(grid,row,leading+day-1);
        assert(cell.height>=line+16+12);
        assert(dayAtPoint(grid,row,leading,days,cell.x+cell.width/2,cell.y+cell.height/2)==int(day));
        assert(dayAtPoint(grid,row,leading,days,cell.x+cell.width,cell.y)==0);
        auto hit=visibleHit(cell,Rect{40,200,width,700});
        assert(hit.width>=0 && hit.height>=0);
        if(hit.width && hit.height) assert(hit.y>=200 && hit.y+hit.height<=900);
      }
      assert(dayAtPoint(grid,row,leading,days,grid.x-1,grid.y)==0);
      assert(dayAtPoint(grid,row,leading,days,grid.x,grid.y+grid.height)==0);
    }
    const int label=line*2, value=line+8, card=summaryCardHeight(value,label);
    assert(card>=24+value+4+label+24);
  }
}
''', include_dirs=(ROOT/'src',))

    def test_wrapped_paragraph_keeps_one_fallback_and_restores_nested_scope(self):
        header=(ROOT/'lib/GfxRenderer/GfxRenderer.h').read_text()
        source=(ROOT/'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        scope=method(header,'class TextFontScope')+';'
        run_cpp(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <cstdint>
#include "Utf8.cpp"
struct EpdFontFamily {
  enum Style {REGULAR};
  const char* coverage;
  bool hasCodepoint(uint32_t cp,Style) const {
    for(const char* p=coverage;*p;++p) if(uint32_t(*p)==cp) return true;
    return false;
  }
};
struct GfxRenderer {
  mutable int fixedTextFontId_=0;
  std::map<int,EpdFontFamily> fontMap{{1,{"A"}},{2,{"B"}},{3,{"AB"}}};
  std::map<int,std::array<int,2>> fallbackFontMap_{{1,{2,3}}};
  int resolveFontFamilyId(int id) const {return id;}
  int resolveTextFontId(int,const char*,EpdFontFamily::Style) const;
''' + scope + r'''
};
''' + method(source,'int GfxRenderer::resolveTextFontId(') + r'''
int main(){
  GfxRenderer r;
  assert(r.resolveTextFontId(1,"A",EpdFontFamily::REGULAR)==1);
  const int paragraph=r.resolveTextFontId(1,"AB",EpdFontFamily::REGULAR);
  assert(paragraph==3);
  {
    GfxRenderer::TextFontScope stable(r,paragraph);
    assert(r.resolveTextFontId(1,"A",EpdFontFamily::REGULAR)==3);
    assert(r.resolveTextFontId(1,"B",EpdFontFamily::REGULAR)==3);
    { GfxRenderer::TextFontScope nested(r,2);
      assert(r.resolveTextFontId(1,"A",EpdFontFamily::REGULAR)==2); }
    assert(r.resolveTextFontId(1,"A",EpdFontFamily::REGULAR)==3);
  }
  assert(r.resolveTextFontId(1,"A",EpdFontFamily::REGULAR)==1);
}
''', include_dirs=(ROOT/'lib/Utf8',))

    def test_text_cannot_replace_a_parent_viewport_clip(self):
        source=(ROOT/'src/components/RickyDesignUi.h').read_text()
        run_cpp(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include "FreeInkUICore.h"
namespace fui=freeink::ui;
namespace ds=ricky::ds;
constexpr int BODY=1;
namespace freeink::ui {struct GfxRendererTarget {static constexpr int FONT_BODY=0;};}
struct GfxRenderer {
  int textFontId(int font,const char*) const {return font;}
  struct TextFontScope {TextFontScope(const GfxRenderer&,int){}};
  mutable std::array<int,4> clip{40,200,604,200};
  mutable int draws=0;
  std::array<int,4> getClipRect() const {return clip;}
  struct ClipScope {
    const GfxRenderer& r; std::array<int,4> old;
    ClipScope(const GfxRenderer& r,int x,int y,int w,int h):r(r),old(r.clip){r.clip={x,y,w,h};}
    ~ClipScope(){r.clip=old;}
  };
};
int fontFor(const GfxRenderer&,int font,const char*) {return font;}
struct RickyUiTarget:fui::DrawTarget {
  const GfxRenderer& r;
  explicit RickyUiTarget(const GfxRenderer& r):r(r){}
  void setFont(int,int){}
  int16_t lineHeight(fui::FontId) const override{return 52;}
  fui::Size measureText(fui::FontId,const char* s,fui::TextStyle) const override{return {int16_t(strlen(s)*8),52};}
  void text(fui::Rect,const char*,fui::TextStyle) override {
    assert(r.clip[0]>=40 && r.clip[1]>=200);
    assert(r.clip[0]+r.clip[2]<=644 && r.clip[1]+r.clip[3]<=400);
    ++r.draws;
  }
  void fill(fui::Rect,fui::Paint,uint8_t,uint8_t) override{}
  void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t) override{}
  void line(fui::Point,fui::Point,uint8_t,fui::Paint) override{}
  void triangle(fui::Point,fui::Point,fui::Point,fui::Paint) override{}
  void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation) override{}
};
''' + method(source,'inline void text(') + r'''
int main(){
  for(int y:{100,180,200,380,450}) for(int h:{20,52,160}){
    GfxRenderer renderer; auto before=renderer.clip;
    text(renderer,{0,int16_t(y),684,int16_t(h)},"A long wrapped statistics label",BODY,false,fui::TextAlign::Left,3);
    assert(renderer.clip==before);
  }
}
''', include_dirs=(ROOT/'freeink-sdk/libs/ui/FreeInkUI/include',))

if __name__=='__main__': unittest.main()
