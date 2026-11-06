"""Native CSS panel tones: rounded spans, borders, clipping and strip fallback."""
from pathlib import Path
import os
import unittest
from test_reading_ui_regressions import method, run_cpp
ROOT = Path(os.environ.get('RICKYOS_READER_SOURCE_ROOT', Path(__file__).resolve().parents[2]))
class ReaderPanelToneTest(unittest.TestCase):
    def test_native_rounded_panels_preserve_luminance_and_transparent_interior(self):
        source = (ROOT/'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        paint = method(source, 'void GfxRenderer::drawGrayscale16RoundedRect(')
        run_cpp(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
struct FontCache { bool scanning=false; bool isScanning() const { return scanning; } };
struct GfxRenderer {
  uint8_t* grayscale16Buffer=nullptr;
  FontCache* fontCacheManager_=nullptr;
  bool _stripActive=false;
  int clipLeft_=2,clipTop_=3,clipRight_=30,clipBottom_=29;
  mutable std::array<uint8_t,32*32> pixels;
  int getScreenWidth() const { return 32; }
  int getScreenHeight() const { return 32; }
  void drawGrayscale16Pixel(int x,int y,uint8_t gray) const {
    if(x>=clipLeft_ && x<clipRight_ && y>=clipTop_ && y<clipBottom_)
      pixels[y*32+x]=(unsigned(gray)+8)/17;
  }
  void fillGrayscale16Rect(int x,int y,int w,int h,uint8_t level) const {
    assert(h==1 && w>0 && level<=15);
    for(int dx=0;dx<w;++dx) drawGrayscale16Pixel(x+dx,y,level*17);
  }
  void drawGrayscale16RoundedRect(int,int,int,int,int,int,uint8_t) const;
};
''' + paint + r'''
bool inside(int x,int y,int w,int h,int r) {
  if(x<0||y<0||x>=w||y>=h) return false;
  r=std::clamp(r,0,std::min(w,h)/2);
  const int dx=x<r?r-x:(x>=w-r?x-(w-r-1):0);
  const int dy=y<r?r-y:(y>=h-r?y-(h-r-1):0);
  return dx*dx+dy*dy<=r*r;
}
int main(){
 for(bool strip:{false,true}) for(int x:{-4,0,5,27}) for(int y:{-3,0,6,28})
 for(int w:{1,2,7,28}) for(int h:{1,2,9,26}) for(int radius:{0,1,4,40})
 for(int stroke:{0,1,2,8}) for(uint8_t gray:{uint8_t(0),uint8_t(128),uint8_t(186),uint8_t(247),uint8_t(255)}) {
   GfxRenderer r;uint8_t marker=0;r.grayscale16Buffer=&marker;r._stripActive=strip;r.pixels.fill(6);
   r.drawGrayscale16RoundedRect(x,y,w,h,stroke,radius,gray);
   int rad=std::clamp(radius,0,std::min(w,h)/2),s=std::min(stroke,std::min(w,h)/2+1);
   for(int py=0;py<32;++py) for(int px=0;px<32;++px){
     bool draw=px>=2&&px<30&&py>=3&&py<29&&inside(px-x,py-y,w,h,rad);
     if(stroke>0&&inside(px-x-s,py-y-s,w-2*s,h-2*s,std::max(0,rad-s)))draw=false;
     assert(r.pixels[py*32+px]==(draw?(unsigned(gray)+8)/17:6));
   }
 }
 GfxRenderer r;r.pixels.fill(6);r.drawGrayscale16RoundedRect(0,0,30,30,0,8,247);
 for(auto p:r.pixels)assert(p==6); // no native transaction: no writes
 uint8_t marker=0;FontCache cache;cache.scanning=true;r.fontCacheManager_=&cache;r.grayscale16Buffer=&marker;
 r.drawGrayscale16RoundedRect(0,0,30,30,0,8,247);for(auto p:r.pixels)assert(p==6);
}
''')
    def test_page_panel_tones_and_page_fragments_use_stored_css_values(self):
        panel = method((ROOT/'lib/Epub/Epub/Page.cpp').read_text(), 'void renderBlockBoxes(')
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
struct BlockBox { int id=1,left=8,width=100,padTop=8,padBottom=8,border=2,radius=8; uint8_t fillLuma=247,borderLuma=186; };
struct BlockStyle { BlockBox box; };
struct TextBlock { BlockStyle style; const auto& getBlockStyle() const { return style; } };
enum { TAG_PageLine=1, TAG_PageImage=2 };
struct PageElement { int yPos=0; virtual int getTag() const=0; virtual ~PageElement()=default; };
struct PageLine:PageElement { TextBlock block; int getTag() const override { return TAG_PageLine; }
 const TextBlock* getBlock() const { return &block; } };
struct PageImage:PageElement { int getTag() const override { return TAG_PageImage; } };
struct GfxRenderer {
 struct Call { int x,y,w,h,stroke,radius; uint8_t gray; };
 std::vector<Call> calls;bool native=true;int bw=0;
 int getLineHeight(int) const { return 20; } bool isGrayscale16Active() const { return native; }
 void drawGrayscale16RoundedRect(int x,int y,int w,int h,int stroke,int radius,uint8_t gray) { calls.push_back({x,y,w,h,stroke,radius,gray}); }
 void drawRoundedRect(int,int,int,int,int,int,bool) { ++bw; }
};
''' + panel + r'''
int main(){
 std::vector<std::unique_ptr<PageElement>> elements;
 for(int y:{10,30,50}) {auto line=std::make_unique<PageLine>();line->yPos=y;elements.push_back(std::move(line));}
 GfxRenderer r;renderBlockBoxes(elements,r,0,5,6);
 assert(r.calls.size()==2 && r.calls[0].gray==247 && r.calls[1].gray==186);
 assert(r.calls[0].y==8 && r.calls[0].h==76 && r.calls[0].stroke==0 && r.calls[1].stroke==2);
 // Next page owns its own panel fragment; the previous page's y must not carry over.
 elements.erase(elements.begin()+1,elements.end());elements[0]->yPos=0;
 r.calls.clear();renderBlockBoxes(elements,r,0,5,6);
 assert(r.calls.size()==2 && r.calls[0].y==0 && r.calls[0].h==34);
 // Border-only panels keep the content beneath their transparent interior.
 static_cast<PageLine&>(*elements[0]).block.style.box.fillLuma=255;
 r.calls.clear();renderBlockBoxes(elements,r,0,5,6);assert(r.calls.size()==1 && r.calls[0].stroke==2);
 r.native=false;r.calls.clear();renderBlockBoxes(elements,r,0,5,6);assert(r.calls.empty() && r.bw==1);
}
''')
if __name__=='__main__': unittest.main()
