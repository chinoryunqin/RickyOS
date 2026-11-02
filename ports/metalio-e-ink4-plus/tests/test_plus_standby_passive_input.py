"""Keep ignored passive-screen input from postponing sleep; retain picker and key actions."""
from pathlib import Path
import unittest

from test_battery_stability import method, run_cpp

PORT = Path(__file__).resolve().parents[1]
SOURCE = (PORT / 'firmware/rickyos-plus-1.2/upstream/src/activities/apps/standby/StandbyActivity.cpp').read_text()


class PassiveInputTest(unittest.TestCase):
    def test_ignored_taps_swipes_and_releases_do_not_keep_plus_awake(self):
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#define FREEINK_DEVICE_METALIO_PLUS 1
#define STANDBY_DIAG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
uint32_t now=1000;uint32_t millis(){return now;}
struct {bool standbyShortcutEnabled=true;unsigned long offMs=0;unsigned long getStandbyPowerOffMs(){return offMs;}} SETTINGS;
struct MappedInputManager {
 enum class Button {Back,Confirm};enum class SwipeDir {None,Right};
 bool back=false,confirm=false,tap=false,swipe=false,released=false,gesture=false;
 bool wasBackGesture(){return gesture;}
 bool wasReleased(Button b){return b==Button::Back?back:confirm;}
 bool wasScreenTapped(int&,int&){return tap;}
 bool wasAnyReleased(){return released;}
 SwipeDir wasSwipe(){return swipe?SwipeDir::Right:SwipeDir::None;}
} mappedInput;
struct StandbyFace {
 enum class PictureAction {Choose,Download};
 bool missing=false;PictureAction action=PictureAction::Choose;
 bool needsPicture(){return missing;}
 PictureAction pictureActionAt(int,int){return action;}
 void onEnter(){}
} face;
struct ActivityResult{};struct RickyWallpaperDownloadActivity{};
struct {int powerOffs=0;void requestPowerOff(){++powerOffs;}} activityManager;
struct StandbyActivity {
 StandbyFace* currentFace_=&face;
 uint32_t lastInputMs_=0,enteredMs_=0;bool drewFrame_=false;
 unsigned finishes=0,pumps=0,sleeps=0,pickers=0,downloads=0,ticks=0;
 void loop();void finish(){++finishes;}void requestUpdate(){}
 void openPicturePicker(){++pickers;}
 template<class T,class F>bool startActivityForResultWith(F){++downloads;return true;}
 void pumpTimeSync(){++pumps;}
 bool tryLightSleep(uint32_t idle){if(idle<35000)return false;++sleeps;return true;}
 void processFaceTick(bool){++ticks;}
};
''' + method(SOURCE, 'void StandbyActivity::loop()') + r'''
int main(){
 for(int input=0;input<4;++input){
  StandbyActivity a;mappedInput={};face={};now=0;
  for(unsigned n=0;n<400;++n){
   now=n*100;mappedInput.tap=input==0;mappedInput.swipe=input==1;
   mappedInput.confirm=input==2;mappedInput.released=input==3;
   a.loop();
  }
  assert(a.lastInputMs_==0&&a.sleeps>0&&a.pumps==400&&a.pickers==0&&a.downloads==0&&a.finishes==0);
 }
 mappedInput={};StandbyActivity picker;face.missing=true;now=1234;mappedInput.tap=true;
 picker.loop();assert(picker.pickers==1&&picker.lastInputMs_==1234&&picker.pumps==0);
 face.action=StandbyFace::PictureAction::Download;picker.loop();assert(picker.downloads==1);
 // Back remains an independent physical shortcut, including while no image exists.
 mappedInput={};mappedInput.back=true;StandbyActivity key;key.loop();assert(key.finishes==1);
 SETTINGS.standbyShortcutEnabled=false;key.loop();assert(key.finishes==1);
 mappedInput={};face={};SETTINGS.offMs=2000;now=3000;
 StandbyActivity deadline;mappedInput.tap=true;deadline.loop();assert(activityManager.powerOffs==1);
}
''')


if __name__ == '__main__':
    unittest.main()
