"""Exercise the production standby entry and radio teardown across clock states."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_battery_stability import method

PORT = Path(__file__).resolve().parents[1]
SOURCE = (PORT / 'firmware/rickyos-plus-1.2/upstream/src/activities/apps/standby/StandbyActivity.cpp').read_text()


class StandbyWifiTest(unittest.TestCase):
    def test_entry_releases_inherited_wifi_without_breaking_clock_sync(self):
        program = r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
[[maybe_unused]] constexpr int WIFI_MODE_NULL=0,WIFI_OFF=0;
struct Wifi {
 int current=0,disconnects=0,stops=0;
 int getMode(){return current;}
 void disconnect(bool erase){assert(!erase);++disconnects;}
 void mode(int value){assert(value==WIFI_OFF);current=value;++stops;}
} WiFi;
int deinits=0;
void esp_wifi_deinit(){assert(WiFi.current==0);++deinits;}
void delay(unsigned ms){assert(ms==100);}
uint32_t millis(){return 1234;}
bool clockValid=true;
namespace TimeUtils {bool isClockValid(){return clockValid;}}
struct Activity {void onEnter(){}};
namespace HalRecorder {void stop(){}}
namespace HalAudioOutput {void shutdown(){}}
struct MappedInput {void stopHapticFeedback(){}};
[[maybe_unused]] MappedInput mappedInput;
struct Light {bool isOn(){return false;}bool present(){return true;}void setOn(bool){}} Frontlight;
struct HalPowerManager {static void beginStandbyKeyWatch(){}};
struct CrossPointSettings {static constexpr int RICKY_STANDBY_CALENDAR=2;};
struct {int rickyStandbyFace=0;bool standbyCalendarReversed=false,clockAutoSync=true;} SETTINGS;
struct GfxRenderer {enum class Orientation {Portrait,LandscapeCounterClockwise,LandscapeClockwise};};
struct Renderer {
 GfxRenderer::Orientation getOrientation(){return GfxRenderer::Orientation::Portrait;}
 void setOrientation(GfxRenderer::Orientation){}
 int getScreenWidth(){return 684;}int getScreenHeight(){return 1216;}
} renderer;
struct Face {bool clock=false;void onEnter(){}bool wantsClock(){return clock;}} face;
struct FacePtr {
 Face* ptr=nullptr;explicit operator bool()const{return ptr;}
 Face* operator->(){return ptr;}
};
struct FaceEntry {bool isAvailable(int,int){return true;}FacePtr create(){return {&face};}};
FaceEntry kFaces[1];constexpr uint8_t kFaceCount=1;
uint8_t availableFaceIndexFromRank(int,int,int){return 0;}
struct {void goToApps(){assert(false);}} activityManager;
struct StandbyActivity:Activity {
 enum class SyncState {Idle,Delayed};enum class DisplayMode {Immersive};
 SyncState syncState_=SyncState::Idle;DisplayMode mode_=DisplayMode::Immersive;
 bool savedFrontlightOn_=false,inverseMode_=false,drewFrame_=false;
 GfxRenderer::Orientation savedOrientation_=GfxRenderer::Orientation::Portrait;
 uint8_t faceIndex_=0;FacePtr currentFace_;
 uint32_t lastInputMs_=0,enteredMs_=0,syncStartMs_=0;
 int updates=0;
 void requestUpdate(){++updates;}
 void onEnter();void stopTimeSyncWifi();
};
''' + method(SOURCE, 'void StandbyActivity::onEnter()') + method(SOURCE, 'void StandbyActivity::stopTimeSyncWifi()') + r'''
int main(){
 for(bool valid:{false,true})for(bool automatic:{false,true})for(bool usesClock:{false,true})for(int mode:{0,1,2,3}){
  StandbyActivity a;clockValid=valid;SETTINGS.clockAutoSync=automatic;face.clock=usesClock;
  WiFi={};WiFi.current=mode;deinits=0;a.onEnter();
  const bool syncNeeded=!valid&&automatic&&usesClock;
  const bool stopped=FREEINK_DEVICE_METALIO_PLUS&&!syncNeeded&&mode!=0;
  assert(WiFi.disconnects==stopped&&WiFi.stops==stopped&&deinits==stopped);
  assert(WiFi.current==(stopped?0:mode));
  assert((a.syncState_==StandbyActivity::SyncState::Delayed)==syncNeeded);
  assert(a.updates==1&&a.enteredMs_==1234);
 }
}
'''
        with tempfile.TemporaryDirectory(prefix='plus-standby-wifi-') as directory:
            root = Path(directory)
            (root / 'test.cpp').write_text(program)
            for plus, emulated in [(1, 0), (0, 0), (0, 1)]:
                subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', f'-DFREEINK_DEVICE_METALIO_PLUS={plus}',
                                f'-DCROSSPOINT_EMULATED={emulated}', str(root / 'test.cpp'),
                                '-o', str(root / 'test')], check=True)
                subprocess.run([str(root / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
