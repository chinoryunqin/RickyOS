"""Exercise the shared header target and production Back/tap routing."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HeaderBackTouchTest(unittest.TestCase):
    def test_gutter_title_and_arrow_route_to_back_without_stealing_rows(self):
        source = (ROOT / 'src/MappedInputManager.cpp').read_text()
        methods = '\n'.join(re.search(r'bool MappedInputManager::'+name+r'\(.*?\n}', source, re.S).group()
                            for name in ('wasBackGesture', 'wasScreenTapped'))
        program = r'''
#include <cassert>
#include "FreeInkUICore.h"
#include "components/HeaderBackTapTarget.h"
namespace fui=freeink::ui;
struct {bool rickyGestureBack=false;} SETTINGS;
struct MappedInputManager {
  struct GPIO {
    bool tap=true;float x=0,y=0;
    bool wasTouchTap(float& nx,float& ny) const {nx=x;ny=y;return tap;}
  } gpio;
  struct Renderer {
    void tapToLogical(float nx,float ny,int& x,int& y) const {x=int(nx);y=int(ny);}
  } renderer;
  void rememberTouchHeldTime() const {}
  bool wasEdgeSwipe(fui::ScreenEdge) const {return false;}
  bool wasBackGesture() const;
  bool wasScreenTapped(int&,int&) const;
};
''' + methods + r'''
int main(){
  // A4 portrait and landscape, plus the larger product header. Insets
  // should never leave a dead strip between the bezel and the back arrow.
  for(int width : {495,716,656,1186}) for(int inset : {0,18,30}) {
    constexpr int y=5,height=64;
    HeaderBackTapTarget::setLeading(inset,y,width,height);
    const int end=inset+width/2;
    for(int x : {0,1,16,32,80,159,end-1}) for(int yy : {y,y+20,y+height-1}) {
      MappedInputManager input;input.gpio.x=x;input.gpio.y=yy;
      int tx=-1,ty=-1;
      assert(input.wasBackGesture());
      assert(!input.wasScreenTapped(tx,ty)); // cannot select a row/control too
    }
    for(auto point : {fui::Point{int16_t(end),y+20},fui::Point{32,y+height},
                     fui::Point{32,y-1}}) {
      MappedInputManager input;input.gpio.x=point.x;input.gpio.y=point.y;
      int tx=-1,ty=-1;
      assert(!input.wasBackGesture() && input.wasScreenTapped(tx,ty));
    }
    HeaderBackTapTarget::clear();
    assert(!HeaderBackTapTarget::contains(32,y+20));
  }
  HeaderBackTapTarget::setLeading(18,5,0,64);
  assert(!HeaderBackTapTarget::contains(0,20));
}
'''
        with tempfile.TemporaryDirectory(prefix='header-back-touch-') as folder:
            cpp, exe = Path(folder)/'check.cpp', Path(folder)/'check'
            cpp.write_text(program)
            subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(ROOT/'src'), '-I'+str(ROOT/'freeink-sdk/libs/ui/FreeInkUI/include'),
                            str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
