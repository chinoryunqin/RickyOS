"""Keep menu dispatch aligned with Reader upstream da7feed5 on touch devices."""
import os
import re
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ControlCenterGestureTest(unittest.TestCase):
    def test_pull_requires_status_origin_and_downward_direction(self):
        source = (ROOT / 'src/MappedInputManager.cpp').read_text()
        method = re.search(r'bool MappedInputManager::wasLightPanelGesture\(\) const \{.*?\n}', source, re.S).group()
        program = r'''
#include <cassert>
#include <cstdlib>
#include <initializer_list>
#include "FreeInkUICore.h"
namespace fui=freeink::ui;
struct {bool rickyGestureControl=true;} SETTINGS;
struct UITheme {
  struct Metrics {int topPadding=5,batteryBarHeight=40;};
  static UITheme& getInstance(){static UITheme theme;return theme;}
  Metrics getMetrics() const {return {};}
  fui::Rect getScreenSafeArea(int) const {return {0,4,531,752};}
};
struct MappedInputManager {
  int renderer=0,sx=100,sy=8,ex=100,ey=300;
  bool touch=true,swipe=true;
  mutable int hits=0;
  bool hasTouch() const {return touch;}
  bool decodeSwipe(int& x,int& y,int& xx,int& yy) const {x=sx;y=sy;xx=ex;yy=ey;return swipe;}
  void rememberTouchHeldTime() const {++hits;}
  bool wasLightPanelGesture() const;
};
''' + method + r'''
int main(){
  MappedInputManager input;
  assert(input.wasLightPanelGesture() && input.hits==1);
  for(int y: {49,80,150,400,700}) {
    input.sy=y;input.ey=y-100; // Up in lists, including near the top edge.
    assert(!input.wasLightPanelGesture());
    input.ey=y+100; // A downward list scroll also belongs to the page.
    assert(!input.wasLightPanelGesture());
  }
  input.sy=8;input.ey=0;
  assert(!input.wasLightPanelGesture());
  input.ey=100;input.ex=300; // horizontal and diagonal motion
  assert(!input.wasLightPanelGesture());
  input.ey=208;
  assert(!input.wasLightPanelGesture());
  input.ex=100;input.ey=300;input.touch=false;
  assert(!input.wasLightPanelGesture());
  input.touch=true;SETTINGS.rickyGestureControl=false;
  assert(!input.wasLightPanelGesture());
  SETTINGS.rickyGestureControl=true;input.swipe=false;
  assert(!input.wasLightPanelGesture());
  assert(input.hits==1);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp, exe = Path(directory)/'check.cpp', Path(directory)/'check'
            cpp.write_text(program)
            subprocess.run(['c++', '-std=c++20', '-I'+str(ROOT/'freeink-sdk/libs/ui/FreeInkUI/include'),
                            str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_dispatch_and_input_ownership(self):
        source = (ROOT / "src/activities/ActivityManager.cpp").read_text()
        start = source.index("void ActivityManager::loop() {")
        end = source.index("  while (pendingAction.load() != PendingAction::None)", start)
        dispatch = source[start:end] + "}\n"
        harness = r'''
#include <atomic>
#include "HeaderBackTapTarget.h"
#include <cassert>
#include <memory>
#include <string>
#include "activities/MainTab.h"
#define LOG_ERR(...) ((void)0)
enum { eIncrement };
void xTaskNotify(int, int, int) {}
struct Activity {
  std::string name = "InxRecent";
  bool reader = false, exclusive = false;
  bool mainTabs = false;
  Rect status{3, 9, 474, MainTabs::statusBarHeight};
  int loops = 0;
  bool requiresExclusiveStorageLoop() { return exclusive; }
  bool isReaderActivity() { return reader; }
  bool isHomeActivity() { return false; }
  bool handleHomeGesture() { return false; }
  bool usesMainTabBar() { return mainTabs; }
  MainTabLayout mainTabLayout() { return {Rect{}, status, Rect{}}; }
  void loop() { ++loops; }
};
struct Input {
  static inline bool a4 = false;
  static bool powerKeyIsBack() { return a4; }
  enum class SwipeDir { None, Up, Down, Left, Right };
  bool topSwipe = true, light = false, tap = false, suppressed = false;
  bool touch = true;
  int tapX = 20, tapY = 10;
  SwipeDir swipe = SwipeDir::None;
  SwipeDir wasSwipe() { return swipe; }
  bool consumeSuppressedRelease() { return suppressed; }
  bool wasHomeGesture() { return false; }
  bool hasTouch() { return touch; }
  bool wasScreenTapped(int& x, int& y) { x = tapX; y = tapY; return tap; }
  bool wasLightPanelGesture() { return light && topSwipe; }
  bool wasMenuGesture() { return topSwipe; }
};
using MappedInputManager = Input;
struct FrontlightPanelActivity { FrontlightPanelActivity(int, Input&, bool = false) {} };
bool failAllocation = false;
template<class T, class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return failAllocation ? nullptr : std::make_unique<T>(std::forward<Args>(args)...);
}
struct { int rickyGestureStatus = 1; } SETTINGS;
struct ActivityManager {
  enum class PendingAction { None, Push };
  std::atomic<PendingAction> pendingAction{PendingAction::None};
  std::atomic<bool> requestedUpdate{false};
  Activity* currentActivity;
  Input mappedInput;
  int renderer = 0, renderTaskHandle = 0, pushes = 0;
  bool handleMainTabInput() { return false; }
  bool standbyShowing() const { return currentActivity && currentActivity->name == "Standby"; }
  bool handleHomeStandbyInput() { return false; }
  void resetHomeStandbyInput() {}
  void trackInputMode() {}  // touch/key focus visibility, covered by its own tests
  void goHome() { assert(false); }
  void pushActivity(std::unique_ptr<FrontlightPanelActivity>) {
    ++pushes; pendingAction = PendingAction::Push;
  }
  void loop();
};
'''
        cases = r'''
int main() {
  const auto check = [](Activity page, Input input, bool opens, bool runs) {
    ActivityManager manager{.currentActivity = &page, .mappedInput = input};
    manager.loop();
    assert(manager.pushes == int(opens));
    assert(page.loops == int(runs)); // Opening must not also scroll/turn a page.
    if (opens) {
      manager.loop(); // A pending push cannot enqueue a second panel.
      assert(manager.pushes == 1 && page.loops == 0);
    }
  };
  for (bool light : {false, true})
    for (bool reader : {false, true})
      check({.reader = reader}, {.light = light}, light, !light);

  for (const char* name : {"InxRecent", "FileBrowser", "Settings", "Home", "FrontlightPanel"}) {
    const bool opens = std::string(name) != "FrontlightPanel";
    check({.name = name}, {.light = true}, opens, !opens);
  }
  for (const char* name : {"Home", "FileBrowser", "Settings", "NetworkModeSelection", "InxRecent", "EpubReader", "FrontlightPanel"}) {
    const std::string page(name);
    const bool opens = page == "Home" || page == "FileBrowser" ||
                       page == "Settings" || page == "NetworkModeSelection";
    check({.name = name}, {.topSwipe = false, .tap = true}, opens, !opens);
  }
  for (const char* name : {"InxRecent", "FileBrowser", "AppsMenu", "Settings", "ReadingStats"}) {
    for (int x : {2, 3, 476, 477})
      for (int y : {8, 9, 36, 37, 40, 41, 42, 43, 46, 47, 52, 53}) {
        const bool opens = x >= 3 && x < 477 && y >= 9 && y < 9 + MainTabs::statusBarHeight;
        check({.name = name, .mainTabs = true},
              {.topSwipe = false, .tap = true, .tapX = x, .tapY = y}, opens, !opens);
      }
    // Top tabs have no status bar; button-only devices ignore touch input.
    check({.name = name, .mainTabs = true, .status = Rect{3, 9, 474, 0}},
          {.topSwipe = false, .tap = true}, false, true);
    check({.name = name, .mainTabs = true},
          {.topSwipe = false, .tap = true, .touch = false}, false, true);
  }
  check({}, {.light = true, .suppressed = true}, false, false);
  check({.exclusive = true}, {.light = true}, false, true);
  check({}, {.topSwipe = false}, false, true);
  for (auto swipe : {Input::SwipeDir::Up, Input::SwipeDir::Down, Input::SwipeDir::Left, Input::SwipeDir::Right}) {
    // Even a backend exposing a stale status tap alongside a swipe must
    // leave ordinary scrolling to the current page.
    check({.name="Settings", .mainTabs=true}, {.topSwipe=false, .tap=true, .swipe=swipe}, false, true);
  }
  Input::a4 = true;
  for (const char* name : {"Home", "Settings", "FileBrowser", "RickyProfile", "EpubReader"}) {
    // Even when a driver calls the end of an upward gesture a status tap,
    // the A4 cannot open its panel without a complete top-origin pull.
    check({.name=name, .mainTabs=true}, {.topSwipe=false, .tap=true}, false, true);
    check({.name=name}, {.light=true}, true, false);
  }
  Input::a4 = false;
  failAllocation = true;
  check({}, {.light = true}, false, false); // OOM does not leak the gesture.
}

'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "check.cpp"
            exe = Path(directory) / "check"
            cpp.write_text(harness + dispatch + cases)
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-I" + str(ROOT / "src"), "-I" + str(ROOT / "src/components"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
