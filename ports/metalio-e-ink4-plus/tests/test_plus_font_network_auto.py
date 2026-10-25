"""Compile actual automatic runner; inject file ownership/stop/failure cases."""
from pathlib import Path
import subprocess
import tempfile

PORT = Path(__file__).resolve().parents[1] / 'firmware/rickyos-plus-1.2/components/rickyos_application/port'
COMMON = r'''#pragma once
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
inline unsigned long tick=0;
inline unsigned fetched=0, constructed=0, destroyed=0, removed=0, snapshots=0;
inline unsigned locks=0;
inline unsigned renderWaits=0;
constexpr unsigned CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384;
inline unsigned internalLimit=16384;
inline void heap_caps_malloc_extmem_enable(unsigned n){internalLimit=n;}
inline unsigned bodyAttempts=0;
inline bool bodyFails=false;
inline bool fileExists=false, objectOom=false, joinFails=false, connected=true, autoReconnect=true;
inline bool plusNetworkFontBodyOnConnected(){assert(connected);++bodyAttempts;return !bodyFails;}
inline unsigned wereadAttempts=0;
inline int plusNetworkWeReadOnConnected(unsigned){assert(connected);++wereadAttempts;return bodyFails?2:1;}
inline bool copyMode=false; inline int mode=1;
inline std::vector<bool> submittedModes;
inline std::vector<std::string> messages;
constexpr int WL_CONNECTED=3, WIFI_STA=1, WIFI_IF_STA=0, WIFI_STORAGE_RAM=1, ESP_OK=0;
constexpr int O_CREAT=1, O_EXCL=2, O_WRITE=4;
inline unsigned long millis(){return tick;}
inline void delay(unsigned n){tick+=n;}
struct GfxRenderer{}; struct MappedInputManager{};
struct HalPowerManager {struct Lock{Lock(){++locks;}~Lock(){--locks;}};};
struct Clock {void cancelSync(){} bool requestSync(){return true;} void update(){}};
inline Clock halClock;
struct File {bool ok; explicit operator bool()const{return ok;}};
struct Store {
 File open(const char* path,int flags){assert(std::string(path)=="/fonts_manifest.tmp");assert(flags==(O_CREAT|O_EXCL|O_WRITE));
  if(fileExists)return {false};fileExists=true;return {true};}
 bool exists(const char*){return fileExists;}
 bool remove(const char*){assert(fileExists);++removed;fileExists=false;return true;}
};
inline Store Storage;
struct Wifi {
 int STA=0;
 int status(){return connected?WL_CONNECTED:0;}
 bool getAutoReconnect(){return autoReconnect;}
 void setAutoReconnect(bool v){autoReconnect=v;}
 void disconnect(bool off,bool,unsigned=0){connected=false;if(off)mode=0;}
 bool reconnect(){connected=!joinFails;return connected;}
};
inline Wifi WiFi;
struct Net {bool setDefaultInterface(int){return connected;}};
inline Net Network;
namespace NetworkStartup {inline bool setMode(GfxRenderer&,int m){mode=m;return true;}inline void prepare(GfxRenderer&) {}}
struct wifi_config_t {unsigned sentinel=42;};
inline int esp_wifi_get_config(int,wifi_config_t*){return 0;}
inline int esp_wifi_get_protocol(int,uint8_t* p){*p=7;return 0;}
inline int esp_wifi_set_storage(int){return 0;}
inline int esp_wifi_set_protocol(int,uint8_t p){assert(p==7);return 0;}
inline int esp_wifi_set_config(int,wifi_config_t* p){assert(p->sentinel==42);return 0;}
inline void plusNetAllocTraceSnapshot(const char*){++snapshots;}
inline void plusNetAllocTraceSetCopyTx(bool v){copyMode=v;}
struct Size {size_t size()const{return 14;}};
class PlusFontNetworkAutoRunner;
enum class MainTab {None,Recent,Settings};
struct Activity {virtual ~Activity()=default;virtual void onEnter(){}virtual void onExit(){}virtual bool isHomeActivity(){return false;}virtual bool isReaderActivity(){return false;}virtual MainTab mainTab(){return MainTab::None;}};
class FontDownloadActivity : public Activity {
 friend class PlusFontNetworkAutoRunner;
 enum {GROUP_LIST,FAMILY_LIST,ERROR};int state_=ERROR;
 Size families_,files_;
 bool fetchAndParseManifest(){
#if RICKYOS_PLUS_NET_HEAP_POLICY
  assert(internalLimit==2048);
#else
  assert(internalLimit==16384);
#endif
  submittedModes.push_back(copyMode);++fetched;tick+=1000;return fetched%4!=0;
 }
public:
 FontDownloadActivity(GfxRenderer&,MappedInputManager&){++constructed;}
 ~FontDownloadActivity(){++destroyed;}
 void onEnter() override {state_=fetchAndParseManifest()?GROUP_LIST:ERROR;}
 void onExit() override {connected=false;mode=0;}
};
#include <memory>
struct Home:Activity {bool isHomeActivity()override{return false;}MainTab mainTab()override{return MainTab::Recent;}};
struct Reader:Activity {bool isReaderActivity()override{return true;}};
struct Setting:Activity {MainTab mainTab()override{return MainTab::Settings;}};
struct FontLibraryActivity:Activity {FontLibraryActivity(GfxRenderer&,MappedInputManager&) {}};
struct Manager {
 std::unique_ptr<Activity> currentActivity=std::make_unique<Home>(),pending;
 std::vector<std::unique_ptr<Activity>> parents;
 bool pop=false;
 unsigned renderBusy=0;
 bool isSwitchPending(){return bool(pending)||pop;}
 void pushActivity(std::unique_ptr<Activity>&& p){pending=std::move(p);}
 void popActivity(){pop=true;}
 void goHome(){if(currentActivity)currentActivity->onExit();currentActivity=std::make_unique<Home>();pending.reset();parents.clear();pop=false;}
 void goToSettings(){goHome();currentActivity=std::make_unique<Setting>();}
 void requestUpdateAndWait(){assert(currentActivity&&!isSwitchPending());++renderWaits;renderBusy=2;}
 void loop(){
  if(isSwitchPending()&&renderBusy){--renderBusy;return;}
  if(pending){parents.push_back(std::move(currentActivity));currentActivity=std::move(pending);currentActivity->onEnter();}
  if(pop){assert(!parents.empty());currentActivity->onExit();currentActivity.reset();currentActivity=std::move(parents.back());parents.pop_back();pop=false;}
 }
};
inline Manager activityManager;
'''
MEMORY = r'''#pragma once
#include "Common.h"
#include <memory>
#include <new>
#include <utility>
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){
 if(objectOom)return {};return std::unique_ptr<T>(new(std::nothrow) T(std::forward<A>(a)...));}
'''
MAIN = r'''
#include "runner.cpp"
#include <cstdarg>
#include <cstdio>
MappedInputManager mappedInputManager;
void record(const char*,const char* format,...){char text[512];va_list a;va_start(a,format);vsnprintf(text,sizeof(text),format,a);va_end(a);messages.emplace_back(text);}
bool has(const std::string& s){for(const auto& m:messages)if(m.find(s)!=std::string::npos)return true;return false;}
void reset(){tick=0;fetched=constructed=destroyed=removed=snapshots=0;fileExists=objectOom=joinFails=false;
 connected=autoReconnect=true;copyMode=false;mode=1;submittedModes.clear();messages.clear();renderWaits=0;bodyAttempts=0;wereadAttempts=0;bodyFails=false;activityManager=Manager{};}
int main(){GfxRenderer renderer;
 reset();plusFontNetworkAutoRun(renderer);
 assert(fetched==12&&constructed==12&&destroyed==12&&removed==12&&!fileExists);
 assert(snapshots==12&&autoReconnect&&!connected&&mode==0&&!copyMode&&locks==0);
 for(unsigned i=0;i<12;++i)assert(submittedModes[i]==(i>=4&&i<8));
 assert(has("done attempted=12 passed=9 expected=12"));
 assert(internalLimit==16384);
#if RICKYOS_PLUS_NET_BODY_AUTORUN && RICKYOS_PLUS_NET_UI_AUTORUN
 assert(bodyAttempts==2&&has("body_done attempted=2 passed=2 expected=2"));
 assert(wereadAttempts==3&&has("weread_done attempted=3 passed=3 needs_login=0 expected=3"));
 reset();bodyFails=true;plusFontNetworkAutoRun(renderer);
 assert(fetched==12&&constructed==destroyed&&bodyAttempts==2&&!fileExists);
 assert(has("body_done attempted=2 passed=0 expected=2")&&autoReconnect&&!connected);
 assert(has("weread_done attempted=3 passed=0 needs_login=3 expected=3"));
#else
 assert(bodyAttempts==0);
#endif
#if RICKYOS_PLUS_NET_UI_AUTORUN
 assert(renderWaits==50&&activityManager.currentActivity->mainTab()==MainTab::Recent&&activityManager.parents.empty());
#if RICKYOS_PLUS_NET_AUTORUN_FROM_READER
 reset();activityManager.currentActivity=std::make_unique<Reader>();plusFontNetworkAutoRun(renderer);
 assert(fetched==12&&constructed==destroyed&&activityManager.parents.empty()&&renderWaits==51);
#endif
 // An unknown popup/login is never navigated away from.
 reset();activityManager.currentActivity=std::make_unique<Activity>();plusFontNetworkAutoRun(renderer);
 assert(fetched==0&&constructed==0&&autoReconnect&&connected&&mode==1);
#else
 assert(renderWaits==0);
#endif
 // Existing file is never fetched into or deleted, and network cleanup still runs.
 reset();fileExists=true;plusFontNetworkAutoRun(renderer);
 assert(fetched==0&&removed==0&&fileExists&&constructed==destroyed&&autoReconnect&&mode==0);
 assert(has("temporary_path_busy"));
 assert(internalLimit==16384);
 // OOM and join failure never create the temporary file.
 reset();objectOom=true;plusFontNetworkAutoRun(renderer);
 assert(fetched==0&&constructed==0&&!fileExists&&removed==0&&autoReconnect&&mode==0);
 assert(internalLimit==16384);
 reset();joinFails=true;plusFontNetworkAutoRun(renderer);
 assert(fetched==0&&constructed==destroyed&&!fileExists&&removed==0&&autoReconnect&&mode==0);
 assert(internalLimit==16384);
 reset();connected=false;plusFontNetworkAutoRun(renderer);
 assert(fetched==0&&constructed==0&&removed==0&&autoReconnect&&has("No connected station"));
}
'''

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work/'Common.h').write_text(COMMON)
    for name in ['Arduino.h', 'GfxRenderer.h', 'HalClock.h', 'HalPowerManager.h', 'HalStorage.h',
                 'MappedInputManager.h', 'Network.h', 'NetworkStartup.h', 'WiFi.h', 'esp_wifi.h', 'esp_heap_caps.h', 'sdkconfig.h',
                 'PlusNetAllocTrace.h', 'activities/settings/FontDownloadActivity.h', 'activities/ActivityManager.h',
                 'activities/settings/FontLibraryActivity.h']:
        target = work/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#pragma once\n#include "Common.h"\n')
    (work/'Memory.h').write_text(MEMORY)
    (work/'Logging.h').write_text('#pragma once\nvoid record(const char*,const char*,...);\n#define LOG_INF record\n#define LOG_ERR record\n')
    (work/'runner.cpp').write_bytes((PORT/'PlusFontNetworkAuto.cpp').read_bytes())
    (work/'test.cpp').write_text(MAIN)
    for ui_path in (0, 1):
        for heap_policy in (0, 1):
            binary = work/f'test-{ui_path}-{heap_policy}'
            subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-DRICKYOS_PLUS_NET_AUTORUN=1', '-DRICKYOS_PLUS_NET_COPY_TX=1',
                            f'-DRICKYOS_PLUS_NET_HEAP_POLICY={heap_policy}', '-DRICKYOS_PLUS_NET_BODY_AUTORUN=1',
                            '-DRICKYOS_PLUS_NET_AUTORUN_FROM_READER=1',
                            f'-DRICKYOS_PLUS_NET_UI_AUTORUN={ui_path}', '-I', str(work),
                            str(work/'test.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
print('Actual auto runner: A/B/A ordering, finite rounds, pre-existing file protection, OOM/join failure and cleanup passed')
