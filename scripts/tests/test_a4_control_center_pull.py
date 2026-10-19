"""Replay raw contacts through the real A4 coordinate transform and input gate."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


class A4ControlCenterPullTest(unittest.TestCase):
    def test_raw_contact_lifecycle_all_orientations(self):
        gfx = (ROOT/'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        header = (ROOT/'lib/GfxRenderer/GfxRenderer.h').read_text()
        mapped = (ROOT/'src/MappedInputManager.cpp').read_text()
        enum = re.search(r'enum Orientation \{.*?\n  };', header, re.S).group()
        view = gfx[gfx.index('namespace a4view {'):gfx.index('}  // namespace a4view')+len('}  // namespace a4view')]
        program = r'''
#include <cassert>
#include <algorithm>
#include <cstdint>
#include "util/ControlCenterPull.h"
#include "FreeInkUICore.h"
namespace fui=freeink::ui;
struct GfxRenderer {
  @ENUM@
  Orientation orientation=Portrait;
  int panelWidth=768,panelHeight=552;
  Orientation getOrientation() const {return orientation;}
  int getScreenWidth() const {return (int(orientation)&1)?752:531;}
  int getScreenHeight() const {return (int(orientation)&1)?531:752;}
  void tapToLogical(float,float,int&,int&) const;
};
@VIEW@
@MAP@
@ROTATE@
struct {bool rickyGestureControl=true;} SETTINGS;
struct UITheme {
  struct Metrics {int topPadding=0,batteryBarHeight=24;};
  static UITheme& getInstance(){static UITheme value;return value;}
  Metrics getMetrics() const {return {};}
  fui::Rect getScreenSafeArea(const GfxRenderer& r) const {
    return {0,4,int16_t(r.getScreenWidth()),int16_t(r.getScreenHeight()-8)};
  }
};
struct MappedInputManager {
  struct GPIO {
    bool down=false,held=false,released=false;
    float nx=0,ny=0;
    bool wasTouchDown(float& x,float& y) const {x=nx;y=ny;return down;}
    bool isTouchHeldAt(float& x,float& y) const {x=nx;y=ny;return held;}
    bool wasTouchReleased() const {return released;}
  } gpio;
  GfxRenderer renderer;
  mutable ControlCenterPull controlCenterPull;
  mutable bool controlCenterPullFrame=false;
  bool touch=true;
  bool hasTouch() const {return touch;}
  void rememberTouchHeldTime() const {}
  void updateControlCenterPull() const;
  bool wasLightPanelGesture() const;
  void point(int x,int y,bool down,bool held,bool release) {
    int px=0,py=0;
    rotateCoordinates(renderer.orientation,x,y,&px,&py,768,552);
    gpio.nx=(px+0.25f)/768;gpio.ny=(py+0.25f)/552;
    gpio.down=down;gpio.held=held;gpio.released=release;
    updateControlCenterPull();
  }
  void begin(int x,int y){point(x,y,true,true,false);assert(!wasLightPanelGesture());}
  void move(int x,int y){point(x,y,false,true,false);assert(!wasLightPanelGesture());}
  void release(bool expected) {
    gpio.down=false;gpio.held=false;gpio.released=true;
    updateControlCenterPull();assert(wasLightPanelGesture()==expected);
    gpio.released=false;updateControlCenterPull();assert(!wasLightPanelGesture());
  }
};
@UPDATE@
@GETTER@
int main(){
  static_assert(sizeof(ControlCenterPull)<=40);
  for(int rotation=0;rotation<4;++rotation){
    MappedInputManager input;input.renderer.orientation=static_cast<GfxRenderer::Orientation>(rotation);
    const int bottom=input.renderer.getScreenHeight()-10;
    // A real pull starts at the top and opens only once on release.
    input.begin(200,10);input.move(202,45);input.move(205,150);input.release(true);
    // Upward scrolling, including an end in the status strip.
    input.begin(200,bottom);input.move(200,200);input.move(200,8);input.release(false);
    // Release-only taps and brief new contacts at lift cannot open it.
    input.release(false);
    input.begin(200,8);input.move(202,24);input.release(false);
    // A page scroll down does not become a global pull just because it is vertical.
    input.begin(200,80);input.move(200,250);input.release(false);
    // A short status tap and lateral swipe must remain closed.
    input.begin(200,8);input.release(false);
    input.begin(200,8);input.move(450,65);input.release(false);
    // Pull then reverse, or start by moving upward: cancel the whole contact.
    input.begin(200,8);input.move(200,180);input.move(200,80);input.release(false);
    input.begin(200,24);input.move(200,0);input.move(200,150);input.release(false);
    // Lost contact/Activity reset cannot resurrect an old armed gesture.
    input.begin(200,8);input.move(200,150);
    input.controlCenterPull.reset();input.release(false);
    // A setting change during contact cannot dispatch on release.
    input.begin(200,8);SETTINGS.rickyGestureControl=false;
    input.move(200,150);input.release(false);SETTINGS.rickyGestureControl=true;
    input.begin(200,8);input.move(200,150);
    input.renderer.orientation=static_cast<GfxRenderer::Orientation>((rotation+1)%4);
    input.release(false);
  }
}
'''
        replacements = {
            'ENUM': enum, 'VIEW': view,
            'MAP': method(gfx, 'void GfxRenderer::tapToLogical('),
            'ROTATE': method(gfx, 'static inline void rotateCoordinates('),
            'UPDATE': method(mapped, 'void MappedInputManager::updateControlCenterPull('),
            'GETTER': method(mapped, 'bool MappedInputManager::wasLightPanelGesture('),
        }
        for key, value in replacements.items():
            program = program.replace('@'+key+'@', value)
        with tempfile.TemporaryDirectory(prefix='a4-control-pull-') as folder:
            cpp, exe = Path(folder)/'check.cpp', Path(folder)/'check'
            cpp.write_text(program)
            subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-DSIMULATOR_DEVICE_EEGO_A4=1',
                            '-I'+str(ROOT/'src'), '-I'+str(ROOT/'freeink-sdk/libs/ui/FreeInkUI/include'),
                            str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
