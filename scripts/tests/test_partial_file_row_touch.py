"""Exercise production SDK row geometry and touch dispatch at the viewport fold."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import run_cpp
ROOT=Path(__file__).resolve().parents[2]
class PartialFileRowTouchTest(unittest.TestCase):
 def test_visible_partial_file_opens_without_claiming_pixels_below_viewport(self):
  sdk=ROOT/'freeink-sdk/libs/ui/FreeInkUI'
  run_cpp(r'''
#include <cassert>
#include <cstring>
#include "FreeInkUI.h"
#include "FreeInkUI.cpp"
namespace fui=freeink::ui;
struct Target:fui::DrawTarget {
 int height=36;
 fui::Rect clip{0,0,2000,3000};
 fui::Rect clipRect() const override{return clip;}
 bool setClipRect(fui::Rect r) override{clip=r;return true;}
 fui::Size measureText(fui::FontId,const char* s,fui::TextStyle) const override {
   return {int16_t(strlen(s)*height/2),int16_t(height)};
 }
 int16_t lineHeight(fui::FontId) const override{return height;}
 void text(fui::Rect,const char*,fui::TextStyle) override{}
 void fill(fui::Rect,fui::Paint,uint8_t,uint8_t) override{}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t) override{}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint) override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint) override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation) override{}
};
int main(){
 for(int line:{24,36,52,80})for(int width:{360,592,1120})for(int top:{0,5}) {
  Target target;target.height=line;
  fui::DeviceContext device;device.width=width+40;device.height=3000;device.hasTouch=true;device.minTouchSize=88;
  fui::ListItem items[8];
  for(int i=0;i<8;++i){items[i].label="A long filename that wraps into two text lines.epub";items[i].subtitle="EPUB 27 MB";items[i].actionValue=i;}
  fui::ListProps props;props.items=items;props.count=8;props.topIndex=top;props.action=7;props.inputMask=fui::InputTouch;
  props.labelText.maxLines=2;props.subtitleText.maxLines=1;props.rowHeight=line*3+48;props.rowGap=8;props.rowInset=0;props.sidePadding=8;
  props.scrollIndicator=false;props.partialTrailingRow=true;
  const int row=fui::measureListRow(target,nullptr,width,props,items[0]).height;
  const fui::Rect viewport{20,100,int16_t(width),int16_t(2*(row+8)+row/2)};
  for(bool interactive:{false,true}){
   props.partialTrailingInteractive=interactive;
   fui::InteractionBuffer<24> hits;fui::InputSnapshot input;
   fui::Frame<24> frame(target,device,input,hits);fui::list(frame,viewport,props);
   fui::InputSnapshot tap;tap.touchReleased=true;tap.touchX=width/2;tap.touchY=100+2*(row+8)+row/4;
   const auto event=hits.route(tap);
   assert(bool(event)==interactive);if(interactive)assert(event.action==7 && event.value==top+2);
   tap.touchY=viewport.bottom();assert(!hits.route(tap));
   tap.touchY=100+row+4;assert(!hits.route(tap));
  }
 }
}
''',include_dirs=(sdk/'include',sdk/'src'))
if __name__=='__main__':unittest.main()
