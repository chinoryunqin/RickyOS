"""Replay actual A4 mapped input: Back owns its entire contact, never a list row."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_reading_ui_regressions import method

ROOT = Path(__file__).resolve().parents[2]


class BackContactTest(unittest.TestCase):
    def test_header_reserve_and_contact_ownership(self):
        source = (ROOT/'src/MappedInputManager.cpp').read_text()
        methods = '\n'.join(method(source, name) for name in (
            'void MappedInputManager::updateHeaderBackTouch()',
            'bool MappedInputManager::wasScreenTapped(',
            'bool MappedInputManager::wasScreenTouchDown(',
            'bool MappedInputManager::wasBackGesture()'))
        program = r'''
#include <cassert>
#include "FreeInkUICore.h"
#include "components/HeaderBackTapTarget.h"
#include "components/A4HeaderLayout.h"
namespace fui=freeink::ui;
struct {bool rickyGestureBack=false;} SETTINGS;
constexpr unsigned long TOUCH_DOWN_SELECT_DELAY_MS=90;
struct MappedInputManager {
  mutable bool headerBackContactOwned=false,headerBackTapFrame=false;
  struct GPIO {
    bool down=false,held=false,released=false,tap=false; float x=0,y=0;
    bool wasTouchDown(float& nx,float& ny) const {nx=x;ny=y;return down;}
    bool isTouchHeldAt(float& nx,float& ny) const {nx=x;ny=y;return held;}
    bool isTouchTapCandidate(float& nx,float& ny,unsigned long& ms) const {nx=x;ny=y;ms=120;return held;}
    bool wasTouchReleased() const {return released;}
    bool wasTouchTap(float& nx,float& ny) const {nx=x;ny=y;return tap;}
  } gpio;
  struct Renderer { void tapToLogical(float nx,float ny,int& x,int& y)const{x=int(nx);y=int(ny);} } renderer;
  void rememberTouchHeldTime()const{}
  bool wasEdgeSwipe(fui::ScreenEdge)const{return false;}
  void updateHeaderBackTouch() const; bool wasScreenTapped(int&,int&)const;
  bool wasScreenTouchDown(int&,int&)const; bool wasBackGesture()const;
};
''' + methods + r'''
int main(){
  for(int width:{495,716}) for(int inset:{0,5,18}) {
    const auto b=A4HeaderLayout::backTarget(inset,0,width,A4HeaderLayout::headerHeight);
    assert(b.height==72 && b.width==inset+width/2);
    assert(A4HeaderLayout::visualHeaderHeight==66);
    assert(A4HeaderLayout::headerHeight-b.y-b.height>=12);
    HeaderBackTapTarget::set(b.x,b.y,b.width,b.height);
    for(int y:{b.y,b.y+36,b.y+b.height-1}) {
      MappedInputManager i;int x=0,yy=0;
      i.gpio={true,true,false,false,float(b.x+10),float(y)}; i.updateHeaderBackTouch();
      assert(!i.wasScreenTouchDown(x,yy)); // header cannot select first row
      i.gpio={false,true,false,false,float(b.x+10),float(A4HeaderLayout::headerHeight+8)};
      i.updateHeaderBackTouch();assert(!i.wasScreenTouchDown(x,yy));
      i.gpio.held=false;i.gpio.released=true;i.gpio.tap=true;i.updateHeaderBackTouch();
      assert(i.wasBackGesture() && !i.wasScreenTapped(x,yy)); // drift on release still Back only
      i.gpio={};i.updateHeaderBackTouch();assert(!i.wasBackGesture());
      i.gpio={true,true,false,false,400,float(A4HeaderLayout::headerHeight+8)};
      i.updateHeaderBackTouch();assert(i.wasScreenTouchDown(x,yy));
      i.gpio.down=false;i.gpio.held=false;i.gpio.released=true;i.gpio.tap=true;i.updateHeaderBackTouch();
      assert(!i.wasBackGesture() && i.wasScreenTapped(x,yy)); // actual row remains usable
    }
    // A swipe/long hold that began on Back cannot activate anything on release.
    MappedInputManager i;int x=0,y=0;
    i.gpio={true,true,false,false,float(b.x+10),float(b.y+10)};i.updateHeaderBackTouch();
    i.gpio={false,false,true,false,200,200};i.updateHeaderBackTouch();
    assert(!i.wasBackGesture() && !i.wasScreenTapped(x,y));
  }
}
'''
        with tempfile.TemporaryDirectory(prefix='a4-back-contact-') as d:
            cpp, exe = Path(d)/'check.cpp', Path(d)/'check'
            cpp.write_text(program)
            subprocess.run(['c++','-std=c++20','-DSIMULATOR_DEVICE_EEGO_A4=1',
                            '-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                            '-I'+str(ROOT/'src'),
                            '-I'+str(ROOT/'freeink-sdk/libs/ui/FreeInkUI/include'),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__': unittest.main()
