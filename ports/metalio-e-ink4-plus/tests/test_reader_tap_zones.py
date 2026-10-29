"""Execute production tap mapping, persistence and the existing gesture helpers."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]/'firmware/rickyos-plus-1.2/upstream'
class ReaderTapZonesTest(unittest.TestCase):
 def compile(self,code, extra=()):
  with tempfile.TemporaryDirectory(prefix='ricky-tap-grid-') as directory:
   p=Path(directory);(p/'test.cpp').write_text(code)
   cmd=['clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'src'),*extra,str(p/'test.cpp'),'-o',str(p/'test')]
   subprocess.run(cmd,check=True);subprocess.run([str(p/'test')],check=True)
 def test_geometry_and_persistence(self):
  self.compile(r'''
#include "ReaderTapZonesJson.h"
#include <cassert>
#include <climits>
#include <string>
int main(){
 using namespace readerTapZones;
 Config c;assert(!c.enabled);assert(hasMenu(c.layouts[0])&&hasMenu(c.layouts[1]));
 for(auto size: {std::pair{684,1216},std::pair{1216,684},std::pair{1000,1768},std::pair{1768,1000}}){
  auto [w,h]=size;
  for(int row=0;row<3;++row)for(int col=0;col<3;++col)
   assert(cellAt((2*col+1)*w/6,(2*row+1)*h/6,w,h)==row*3+col);
  assert(cellAt(0,0,w,h)==0);assert(cellAt(w-1,h-1,w,h)==8);
  assert(cellAt(w,0,w,h)==-1&&cellAt(-1,0,w,h)==-1);
  assert(cellAt(0,h,w,h)==-1&&cellAt(0,-1,w,h)==-1);
  assert(cellAt(w/3-1,0,w,h)==0);
  assert(cellAt((w+2)/3,0,w,h)==1);
 }
 assert(cellAt(INT_MAX-1,INT_MAX-1,INT_MAX,INT_MAX)==8);
 JsonDocument old;assert(!load(old.as<JsonVariantConst>()).enabled);
 c.enabled=true;c.layouts[0]=standard();c.layouts[1]=standard();c.layouts[0][0]=value(Action::Ignore);c.layouts[1][8]=value(Action::Refresh);
 JsonDocument doc;save(doc,c);auto d=load(doc.as<JsonVariantConst>());
 assert(d.enabled&&d.layouts==c.layouts);
 std::string json;serializeJson(doc,json);JsonDocument reboot;assert(!deserializeJson(reboot,json));
 d=load(reboot.as<JsonVariantConst>());assert(d.enabled&&d.layouts==c.layouts);
 auto good=json;
 for(const char* bad: {"{}", R"({"readerTapZones":true})", R"({"readerTapZones":{"version":9,"enabled":true}})",
  R"({"readerTapZones":{"version":1,"enabled":true,"portrait":[1,2]}})"}){
  JsonDocument b;deserializeJson(b,bad);assert(!load(b.as<JsonVariantConst>()).enabled);
 }
 for(int wrong:{-1,5,1000}){deserializeJson(doc,good);doc["readerTapZones"]["portrait"][2]=wrong;assert(!load(doc.as<JsonVariantConst>()).enabled);}
 deserializeJson(doc,good);doc["readerTapZones"]["landscape"][4]=1.5;assert(!load(doc.as<JsonVariantConst>()).enabled);
 deserializeJson(doc,good);doc["readerTapZones"]["landscape"][4]=true;assert(!load(doc.as<JsonVariantConst>()).enabled);
 deserializeJson(doc,good);doc["readerTapZones"]["portrait"].as<JsonArray>().add(1);assert(!load(doc.as<JsonVariantConst>()).enabled);
 // Edited valid arrays with no menu recover a reachable centre cell.
 c.layouts[0].fill(0);normalize(c);assert(c.layouts[0][4]==value(Action::Menu));
}
''', ['-I'+str(ROOT/'.pio/libdeps/simulator_rickyos/ArduinoJson/src')])
 def test_real_reader_gesture_routing(self):
  s=(ROOT/'src/activities/reader/ReaderUtils.h').read_text()
  start=s.index('inline bool gestureAllowsSwipe');end=s.index('// Grayscale anti-aliasing pass.')
  production=s[start:end]
  # Keep real tap/menu functions; hardware button and orientation helpers are
  # unrelated, so their minimal interfaces below only satisfy compilation.
  self.compile(r'''
#include "ReaderTapZones.h"
#include <cstdint>
#include <cctype>
#include <string_view>
#include <cassert>
struct CrossPointSettings {
 enum {TAP_AND_SWIPE,TAP_ONLY,INVERTED_TAP,SWIPE_ONLY,OFF,CHAPTER_SKIP};
 enum ORIENTATION{PORTRAIT,LANDSCAPE_CW,INVERTED,LANDSCAPE_CCW};
 enum SHORT_PWRBTN{NONE,PAGE_TURN};
 enum {READER_MENU_TAP,READER_MENU_SWIPE_UP};
 uint8_t touchReaderControls=1,pageTurnGesture=TAP_AND_SWIPE,previousPageGesture=TAP_AND_SWIPE,pageTurnDirection=0,showReaderMenu=READER_MENU_TAP,longPressButtonBehavior=OFF,shortPwrBtn=0;
 readerTapZones::Config readerTapZones;
 bool effectiveTiltPageTurn()const{return false;}
}settings;
#define SETTINGS settings
struct GfxRenderer {enum Orientation{Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};int w=684,h=1216;int getScreenWidth()const{return w;}int getScreenHeight()const{return h;}void setOrientation(Orientation){};};
enum class HomeButtonAction{None,NextPage};
struct MappedInputManager {
 enum class SwipeDir{None,Left,Right,Up,Down};enum class Button{Right,Left,PageBack,PageForward,Power};
 bool touch=true,tap=false,edge=false,bottom=false;int x=0,y=0;SwipeDir swipe=SwipeDir::None;
 bool hasTouch()const{return touch;}bool wasScreenTapped(int&a,int&b)const{a=x;b=y;return tap;}
 SwipeDir wasSwipe()const{return swipe;}bool wasMenuGesture()const{return edge;}bool wasReaderMenuSwipeUp()const{return bottom;}
 bool isNavDirectionSwapped()const{return false;}bool wasPressed(Button)const{return false;}bool wasReleased(Button)const{return false;}
 bool wasLongPressed(Button,unsigned long)const{return false;}HomeButtonAction homeButtonAction()const{return HomeButtonAction::None;}
};
struct{unsigned long lastTouchHeldMs(){return 1200;}}gpio;
struct{bool wasTiltedForward(){return false;}bool wasTiltedBack(){return false;}bool wasBackTappedNext(){return false;}bool wasBackTappedPrevious(){return false;}}halTiltSensor;
namespace ReaderUtils {
constexpr unsigned long SKIP_HOLD_MS=700;
'''+production+r'''
}
int main(){
 GfxRenderer r;MappedInputManager input;input.tap=true;input.x=600;input.y=600;
 auto a=ReaderUtils::detectTouchPageTurn(r,input);assert(a.next&&!a.prev&&a.heldMs==1200);
 input.y=20;a=ReaderUtils::detectTouchPageTurn(r,input);assert(!a.next&&!a.prev&&ReaderUtils::isTouchMenuTap(r,input));
 SETTINGS.readerTapZones.enabled=true;
 for(auto size:{std::pair{684,1216},std::pair{1216,684},std::pair{1000,1768},std::pair{1768,1000}}){r.w=size.first;r.h=size.second;
  auto& layout=SETTINGS.readerTapZones.layouts[readerTapZones::bank(r.w,r.h)];
  for(int cell=0;cell<9;++cell)for(int action=0;action<5;++action){
   layout[cell]=action;input.x=(2*(cell%3)+1)*r.w/6;input.y=(2*(cell/3)+1)*r.h/6;
   for(int hand=0;hand<2;++hand)for(bool rtl:{false,true}){
    SETTINGS.pageTurnDirection=hand;SETTINGS.pageTurnGesture=CrossPointSettings::SWIPE_ONLY;
    SETTINGS.previousPageGesture=CrossPointSettings::SWIPE_ONLY;SETTINGS.showReaderMenu=99;
    a=ReaderUtils::detectTouchPageTurn(r,input,rtl);
    assert(a.prev==(action==1)&&a.next==(action==2)&&a.heldMs==0);
    assert(ReaderUtils::isTouchMenuTap(r,input)==(action==3));
    assert(ReaderUtils::isTouchRefreshTap(r,input)==(action==4));
   }
  }
 }
 // Swipes obey the original gesture/hand logic and are not grid taps.
 input.swipe=MappedInputManager::SwipeDir::Left;SETTINGS.pageTurnDirection=0;
 SETTINGS.pageTurnGesture=CrossPointSettings::TAP_AND_SWIPE;
 a=ReaderUtils::detectTouchPageTurn(r,input);assert(a.next&&!a.prev);
 input.swipe=MappedInputManager::SwipeDir::None;input.tap=false;
 a=ReaderUtils::detectTouchPageTurn(r,input);assert(!a.next&&!a.prev&&!ReaderUtils::isTouchMenuTap(r,input));
 input.tap=true;SETTINGS.touchReaderControls=0;
 a=ReaderUtils::detectTouchPageTurn(r,input);assert(!a.next&&!a.prev&&!ReaderUtils::isTouchRefreshTap(r,input));
 input.edge=true;assert(ReaderUtils::isTouchMenuGesture(r,input));input.edge=false;
 input.touch=false;assert(!ReaderUtils::isTouchMenuGesture(r,input));
}
''')
if __name__=='__main__':unittest.main()
