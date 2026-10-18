"""Run the actual power-key owner with A4 and legacy policies, including wake and hold guards."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'src/main.cpp').read_text()
start=s.index('  {\n    constexpr unsigned long kSideKeyReleaseMs')
end=s.index('\n  if (!kRickyPowerModel',start)
block=s[start:end]
PROGRAM=r'''
#include <cassert>
#include <string>
#define CROSSPOINT_EMULATED 1
#define LOG_DBG(...) ((void)0)
#define STANDBY_DIAG(...) ((void)0)
unsigned long clockMs = 0, allowSleepAt = 0, lastActivityTime = 0;
constexpr unsigned long RICKY_POWER_OFF_HOLD_MS = 2000;
unsigned long millis() { return clockMs; }
struct HalGPIO {
  enum { BTN_POWER, BTN_DOWN };
  bool power = false;
  bool isPressed(int b) const { return b == BTN_POWER && power; }
} gpio;
struct Manager {
  bool home = false, standby = false;
  int opened = 0, closed = 0;
  bool standbyShowing() const { return standby; }
  bool isHomeActivity() const { return home; }
  void openStandby() {
    standby = true;
    ++opened;
  }
  void closeStandby() {
    standby = false;
    ++closed;
  }
} activityManager;
struct Input {
  int backs = 0;
  bool back = false;
  void setBackClickFrame() {
    ++backs;
    back = true;
  }
} mappedInputManager;
int poweredOff = 0;
void enterDeepSleep() { ++poweredOff; }
void pollKey() { ACTUAL_BLOCK }
void step(unsigned long at, bool pressed) {
  clockMs = at;
  gpio.power = pressed;
  mappedInputManager.back = false;
  pollKey();
}
int main(int argc, char **argv) {
  assert(argc == 2);
  std::string mode = argv[1];
  if (mode == "wake-held") {
    step(0, true);
    step(2500, true);
    step(2600, false);
    step(2800, false);
    assert(poweredOff == 0 && mappedInputManager.backs == 0 &&
           activityManager.opened == 0);
  } else {
    step(0, false);
    if (mode == "home")
      activityManager.home = true;
    if (mode == "standby")
      activityManager.standby = true;
    step(100, true);
    if (mode == "hold") {
      step(2100, true);
      assert(poweredOff == 1);
      step(2200, false);
      step(2400, false);
      assert(mappedInputManager.backs == 0 && activityManager.opened == 0);
      return 0;
    }
    if (mode == "bounce") {
      step(150, false);
      step(200, true);
    }
    step(250, true);
    step(251, false);
    step(449, false);
    assert(mappedInputManager.backs == 0 && activityManager.opened == 0);
    step(450, false);
    if (mode == "standby")
      assert(activityManager.closed == 1 && mappedInputManager.backs == 0);
    else if (mode == "home" || !FREEINK_DEVICE_EEGO_A4)
      assert(activityManager.opened == 1 && mappedInputManager.backs == 0);
    else
      assert(mappedInputManager.backs == 1 && mappedInputManager.back &&
             activityManager.opened == 0);
    step(500, false);
    step(800, false);
    assert(!mappedInputManager.back && poweredOff == 0);
    assert(mappedInputManager.backs <= 1 && activityManager.opened <= 1 &&
           activityManager.closed <= 1);
  }
}
'''
with tempfile.TemporaryDirectory(prefix='eego-power-key-') as directory:
 p=Path(directory);source=p/'key.cpp';source.write_text(PROGRAM.replace('ACTUAL_BLOCK',block))
 for a4 in (0,1):
  binary=p/('key'+str(a4))
  subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-DFREEINK_DEVICE_EEGO_A4='+str(a4),str(source),'-o',str(binary)],check=True)
  for scenario in ('back','home','standby','hold','wake-held','bounce'):
   subprocess.run([str(binary),scenario],check=True)
print('A4 and legacy power key: 12 wake, debounce, short-press and hold scenarios passed')
