"""Run the production HAL filter with independent physical/touch preferences."""
from pathlib import Path
import subprocess
import tempfile

source = Path('firmware/rickyos-plus-1.2/upstream/lib/hal/HalGPIO.cpp').read_text()
start = source.index('void HalGPIO::updateHapticFeedback(uint8_t touchEnabled, uint8_t sideKeysEnabled)')
method = source[start:source.index('\nvoid HalGPIO::stopHapticFeedback()', start)]
program = r"""
#include <cassert>
#include <cstdint>
#include <initializer_list>
namespace plus_haptic {int pulses=0,stops=0;void pulse(){++pulses;}void stop(){++stops;}}
class HalGPIO {
 public:
 enum{BTN_BACK,BTN_CONFIRM,BTN_LEFT,BTN_RIGHT,BTN_UP,BTN_DOWN,BTN_POWER};
 struct Input{bool tap=false;unsigned long held=0;bool wasTouchTap(float&,float&)const{return tap;}
   unsigned long lastTouchHeldMs()const{return held;}}inputMgr;
 uint8_t edges=0;bool home=false;
 bool wasHomeKeyPressed()const{return home;}
 bool wasPressed(uint8_t b)const{return edges&(1<<b);}
 void updateHapticFeedback(uint8_t touchEnabled,uint8_t sideKeysEnabled=0);
};
""" + method + r"""
int main(){
 for(int touchOn=0;touchOn<2;++touchOn)for(int keysOn=0;keysOn<2;++keysOn)
 for(int edges=0;edges<128;++edges)for(int home=0;home<2;++home)
 for(int tap=0;tap<2;++tap)for(unsigned held:{0u,499u,500u,900u}){
  HalGPIO h;h.edges=edges;h.home=home;h.inputMgr.tap=tap;h.inputMgr.held=held;
  const bool expected=(touchOn&&(home||(edges&13)||(tap&&held<500)))||(keysOn&&(edges&50));
  const int before=plus_haptic::pulses,stops=plus_haptic::stops;
  h.updateHapticFeedback(touchOn,keysOn);
  assert(plus_haptic::pulses==before+expected);
  assert(plus_haptic::stops==stops+(!touchOn&&!keysOn));
 }
 HalGPIO h;h.edges=1<<HalGPIO::BTN_DOWN;int before=plus_haptic::pulses;
 h.updateHapticFeedback(1);assert(plus_haptic::pulses==before);
 h.edges=0;h.inputMgr.tap=true;h.updateHapticFeedback(1);assert(plus_haptic::pulses==before+1);
 h.inputMgr.tap=false;h.updateHapticFeedback(1,1);assert(plus_haptic::pulses==before+1);
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Actual HAL feedback passed: independent touch/side switches, cover LEFT/RIGHT/Home stay touch, AI/UP/DOWN stay side, '
      'tap vs hold/drag, no power/release-repeat, at most one simultaneous pulse and off suppression.')
