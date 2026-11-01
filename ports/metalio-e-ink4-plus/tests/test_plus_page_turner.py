"""Exercise the shipped BLE key routing and one-way radio reservation policy."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
UP=ROOT/'firmware/rickyos-plus-1.2/upstream'

def method(text,signature):
 a=text.index(signature);b=text.index('{',a)+1;depth=1
 while depth:
  depth+=(text[b]=='{')-(text[b]=='}');b+=1
 return text[a:b]

def run(program,flags=()):
 with tempfile.TemporaryDirectory(prefix='plus-page-turner-') as directory:
  root=Path(directory);source=root/'test.cpp';source.write_text(program);binary=root/'test'
  subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                  '-I',str(UP/'src'),*flags,str(source),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)

s=(UP/'src/MappedInputManager.cpp').read_text()
program=r'''
#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include "BleKeyMapping.h"
namespace freeink {
 enum class SpecialKey : uint8_t {None,Left,Right,Up,Down,PageUp,PageDown};
 struct KeyEvent {SpecialKey special=SpecialKey::None; uint8_t keycode=0;};
}
struct {std::deque<freeink::KeyEvent> events; bool popKey(freeink::KeyEvent& e) {
 if(events.empty()) return false;e=events.front();events.pop_front();return true;}} BleHid;
namespace bleinput {
 bool encodeKey(const freeink::KeyEvent& e,uint8_t& kind,uint8_t& value) {
  kind=e.special==freeink::SpecialKey::None?1:0;value=kind?e.keycode:static_cast<uint8_t>(e.special);return value!=0;
 }
}
struct {bleinput::KeyMap bleKeyMap;} SETTINGS;
struct MappedInputManager {
 enum class Button : uint8_t {PageForward,PageBack,Confirm,Back,Up,Down,Left,Right,Count};
 static constexpr size_t kButtonCount=static_cast<size_t>(Button::Count);
 mutable std::array<bool,kButtonCount> blePressEdges{},bleReleaseEdges{},blePendingEdges{};
 mutable bool bleActivityThisFrame=false,bleCaptureMode=false,bleHasCaptured=false;
 mutable uint8_t bleCapturedKind=0,bleCapturedValue=0;
 void pollBle()const;
};
@HELPER@
@BUTTON@
@POLL@
int main() {
 using freeink::SpecialKey;
 using bleinput::Action;
 for(auto key:{SpecialKey::Right,SpecialKey::Down,SpecialKey::PageDown}) {
  MappedInputManager input; BleHid.events.push_back({key,0});input.pollBle();assert(input.blePressEdges[0]);
  input.pollBle();assert(input.bleReleaseEdges[0]&&!input.blePressEdges[0]);
  input.pollBle();assert(!input.bleReleaseEdges[0]);
 }
 for(auto key:{SpecialKey::Left,SpecialKey::Up,SpecialKey::PageUp}) {
  MappedInputManager input;BleHid.events.push_back({key,0});input.pollBle();assert(input.blePressEdges[1]);
 }
 for(auto key:{0xEA,0xE9}) {
  MappedInputManager input;BleHid.events.push_back({SpecialKey::None,static_cast<uint8_t>(key)});input.pollBle();
  assert(input.blePressEdges[key==0xEA?0:1]);
 }
 assert(bleinput::assign(SETTINGS.bleKeyMap,0,static_cast<uint8_t>(SpecialKey::Right),Action::PageBack));
 MappedInputManager custom;BleHid.events.push_back({SpecialKey::Right,0});custom.pollBle();
 assert(custom.blePressEdges[1]&&!custom.blePressEdges[0]);
 bleinput::Action action;assert(!defaultPageAction(SETTINGS.bleKeyMap,0,static_cast<uint8_t>(SpecialKey::Left),action));
 SETTINGS.bleKeyMap={};
 MappedInputManager capture;capture.bleCaptureMode=true;BleHid.events.push_back({SpecialKey::Right,0});capture.pollBle();
 assert(capture.bleHasCaptured&&!capture.blePressEdges[0]);
 MappedInputManager quick;BleHid.events.push_back({SpecialKey::Right,0});quick.pollBle();
 BleHid.events.push_back({SpecialKey::Right,0});quick.pollBle();assert(quick.bleReleaseEdges[0]&&quick.blePendingEdges[0]);
 quick.pollBle();assert(quick.blePressEdges[0]&&!quick.bleReleaseEdges[0]);
}
'''
program=program.replace('@HELPER@',method(s,'bool defaultPageAction(')).replace('@BUTTON@',method(s,'MappedInputManager::Button buttonForAction(')).replace('@POLL@',method(s,'void MappedInputManager::pollBle() const'))
run(program,['-DFREEINK_CAP_BLE_HID_HOST=1'])

s=(UP/'src/NetworkStartup.cpp').read_text()
program=r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
template<typename... T> void log(T...) {}
#define LOG_INF(...) log(__VA_ARGS__)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
[[maybe_unused]] constexpr int ESP_BT_CONTROLLER_STATUS_IDLE=0,ESP_BT_MODE_BLE=1,ESP_OK=0;
using esp_err_t=int;
int releases=0,status=0;
int esp_bt_controller_get_status(){return status;}
int esp_bt_controller_mem_release(int){++releases;return ESP_OK;}
size_t heap_caps_get_free_size(int){return 100000;}
struct Settings {bool bluetoothEnabled=false;}; [[maybe_unused]] Settings SETTINGS;
bool btReleased=false;
@METHOD@
int main(){
 releaseIdleBluetooth();releaseIdleBluetooth();
#if FREEINK_DEVICE_METALIO_PLUS
 assert(releases==0&&!btReleased); // Wi-Fi use cannot permanently disable the new shortcut.
#else
 assert(releases==1&&btReleased);  // Other boards retain their existing low-memory policy.
#endif
}
'''.replace('@METHOD@',method(s,'void releaseIdleBluetooth()'))
for plus in (0,1):run(program,[f'-DFREEINK_DEVICE_METALIO_PLUS={plus}','-DFREEINK_CAP_BLE_HID_HOST=1'])

cmake=(ROOT/'firmware/rickyos-plus-1.2/components/rickyos_application/CMakeLists.txt').read_text()
for forbidden in ('PlusBtAudio.cpp','BluetoothSpeakerActivity.cpp','RICKYOS_PLUS_BT_DIAG'):
 assert forbidden not in cmake,forbidden
assert 'BluetoothSpeakerActivity' not in (UP/'src/activities/settings/BluetoothSettingsActivity.cpp').read_text()
assert 'PlusBtAudio' not in (ROOT/'firmware/rickyos-plus-1.2/components/rickyos_application/port/PlusAudio.cpp').read_text()

panel=(UP/'src/activities/util/FrontlightPanelActivity.cpp').read_text()
program=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
struct {bool bluetoothEnabled=false; int saves=0; void saveToFile(){++saves;}} SETTINGS;
namespace bleinput {
 enum class StartResult {Started,RadioBusy,LowMemory,Failed,Unavailable};
 enum class StartContext {Explicit};
 bool busy=false,connected=false; int starts=0,stops=0;
 StartResult result=StartResult::Started;
 bool radioBusy(){return busy;} bool isConnected(){return connected;}
 StartResult ensureStarted(int,StartContext){++starts;return result;}
 void stop(){++stops;}
}
enum {STR_BT_WIFI_PAUSED,STR_BT_LOW_MEMORY,STR_BT_UNAVAILABLE};
const char* tr(int value){return value==STR_BT_WIFI_PAUSED?"waiting":value==STR_BT_LOW_MEMORY?"memory":"unavailable";}
unsigned millis(){return 100;}
struct RenderLock {};
struct FrontlightPanelActivity {
 int renderer=0,updates=0;
 unsigned bluetoothBannerUntil=0,nextBluetoothAttemptAt=0;
 char bluetoothBanner[128]{};
 void requestUpdate(){++updates;}
 void toggleBluetooth(); uint8_t bluetoothState()const;
};
@STATE@
@TOGGLE@
int main(){
 FrontlightPanelActivity panel;
 assert(panel.bluetoothState()==0);
 panel.toggleBluetooth();
 assert(SETTINGS.bluetoothEnabled&&SETTINGS.saves==1&&bleinput::starts==1);
 assert(panel.bluetoothState()==2&&panel.nextBluetoothAttemptAt==2100);
 bleinput::connected=true;assert(panel.bluetoothState()==3);
 bleinput::busy=true;assert(panel.bluetoothState()==1);
 panel.toggleBluetooth();assert(!SETTINGS.bluetoothEnabled&&bleinput::stops==1);
 assert(panel.bluetoothState()==0);
 bleinput::result=bleinput::StartResult::RadioBusy;panel.toggleBluetooth();
 assert(SETTINGS.bluetoothEnabled&&std::strcmp(panel.bluetoothBanner,"waiting")==0);
 assert(panel.bluetoothBannerUntil==4100&&bleinput::busy);
 panel.toggleBluetooth();assert(panel.bluetoothBannerUntil==0&&panel.bluetoothBanner[0]==0);
 bleinput::result=bleinput::StartResult::LowMemory;panel.toggleBluetooth();
 assert(std::strcmp(panel.bluetoothBanner,"memory")==0);
 panel.toggleBluetooth();
 bleinput::result=bleinput::StartResult::Failed;panel.toggleBluetooth();
 assert(std::strcmp(panel.bluetoothBanner,"unavailable")==0);
 assert(panel.updates==7&&SETTINGS.saves==7);
}
'''.replace('@STATE@',method(panel,'uint8_t FrontlightPanelActivity::bluetoothState() const')).replace('@TOGGLE@',method(panel,'void FrontlightPanelActivity::toggleBluetooth()'))
run(program,['-DFREEINK_CAP_BLE_HID_HOST=1','-Wno-unused-variable'])

main=(UP/'src/main.cpp').read_text()
program=r'''
#include <cassert>
struct {bool bluetoothEnabled=true;} SETTINGS;
namespace bleinput {
 enum class StartContext {Reader}; enum class StartResult {Started,LowMemory,Failed};
 bool running=true; int starts=0,stops=0;
 bool isRunning(){return running;} void stop(){running=false;++stops;}
 StartResult ensureStarted(int,StartContext){running=true;++starts;return StartResult::Started;}
}
struct Manager {
 bool standby=false,keep=false,exclusive=false,reader=false,defer=false;
 bool standbyShowing()const{return standby;}
 bool keepsBluetoothAlive()const{return !standby&&keep;}
 bool requiresExclusiveStorageLoop()const{return exclusive;}
 bool deferBluetoothStart()const{return defer;}
 bool isReaderActivity()const{return reader;}
} activityManager;
constexpr int WIFI_MODE_NULL=0;
struct {int mode=0;int getMode()const{return mode;}} WiFi;
unsigned millis(){return 100000;}
struct RenderLock {enum class Mode {Try};explicit RenderLock(Mode){}bool ownsLock()const{return true;}};
int renderer=0;
@LIFECYCLE@
void reset(){activityManager={};SETTINGS.bluetoothEnabled=true;bleinput::running=true;WiFi.mode=0;bleinput::starts=bleinput::stops=0;}
int main(){
 reset();updateBluetoothLifecycle();
#if FREEINK_DEVICE_METALIO_PLUS
 assert(bleinput::running&&bleinput::stops==0&&bleinput::starts==0); // pairing -> settings/home
 activityManager.reader=activityManager.keep=true;activityManager.defer=true;
 updateBluetoothLifecycle();assert(bleinput::running&&bleinput::starts==0); // chapter build keeps link
 activityManager.standby=true;updateBluetoothLifecycle();assert(!bleinput::running);
#else
 assert(!bleinput::running); // retain other boards' original page policy
#endif
 reset();WiFi.mode=1;updateBluetoothLifecycle();assert(!bleinput::running);
 reset();activityManager.exclusive=true;updateBluetoothLifecycle();assert(!bleinput::running);
 reset();SETTINGS.bluetoothEnabled=false;updateBluetoothLifecycle();assert(!bleinput::running);
 reset();bleinput::running=false;updateBluetoothLifecycle();assert(!bleinput::running&&bleinput::starts==0);
 activityManager.reader=activityManager.keep=true;updateBluetoothLifecycle();assert(bleinput::running&&bleinput::starts==1);
}
'''.replace('@LIFECYCLE@',method(main,'void updateBluetoothLifecycle()'))
for plus in (0,1):run(program,['-DFREEINK_CAP_BLE_HID_HOST=1',f'-DFREEINK_DEVICE_METALIO_PLUS={plus}'])
program=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
std::deque<uint8_t> fifo;
constexpr unsigned USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT=1;
unsigned acknowledged=0;
void usb_serial_jtag_ll_clr_intsts_mask(unsigned mask){assert(mask==1);++acknowledged;}
int usb_serial_jtag_ll_rxfifo_data_available(){return !fifo.empty();}
int usb_serial_jtag_ll_read_rxfifo(uint8_t* p,uint32_t n){assert(n==1);if(fifo.empty())return 0;*p=fifo.front();fifo.pop_front();return 1;}
void feed(const std::string& s){for(auto c:s)fifo.push_back(static_cast<uint8_t>(c));}
@INPUT@
int main(){
 char out[48]{};
 std::string command="CMD:BLE_DIAG_START\r\n";
 for(size_t cut=0;cut<command.size()-1;++cut){
  feed(command.substr(0,cut));assert(!readPlusBleDiagnosticLine(out,sizeof(out)));
  feed(command.substr(cut));assert(readPlusBleDiagnosticLine(out,sizeof(out)));
  assert(std::strcmp(out,"CMD:BLE_DIAG_START")==0&&fifo.empty());
 }
 feed(std::string(200,'X')+"\nCMD:BLE_DIAG_HOME\n");
 unsigned calls=0;while(!readPlusBleDiagnosticLine(out,sizeof(out)))assert(++calls<5);
 assert(std::strcmp(out,"CMD:BLE_DIAG_HOME")==0&&fifo.empty());
 feed("TOO_LONG\n");assert(!readPlusBleDiagnosticLine(out,2)&&fifo.empty());
 feed("\n");assert(readPlusBleDiagnosticLine(out,sizeof(out))&&out[0]==0);
 assert(acknowledged>0);
}
'''.replace('@INPUT@',method(main,'bool readPlusBleDiagnosticLine('))
run(program)
print('Plus page-turner: keys, overrides, capture, single gesture, radio reservation, control-center toggle/status/errors and audio exclusion passed')
