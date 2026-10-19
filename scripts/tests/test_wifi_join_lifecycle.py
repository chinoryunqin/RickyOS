"""Run production join logic through radio startup, association, DHCP and cancel."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class WifiJoinLifecycleTest(unittest.TestCase):
    def test_join_timer_radio_lifecycle_and_failure_stage(self):
        source=(ROOT/'src/activities/network/WifiSelectionActivity.cpp').read_text()
        header=(ROOT/'src/activities/network/WifiSelectionActivity.h').read_text()
        timeouts=header[header.index('#if (defined(FREEINK_DEVICE_EEGO_A4)',header.index('// Connection timeout')):header.index('  unsigned long connectionStartTime',header.index('// Connection timeout'))]
        methods='\n'.join(method(source,s) for s in (
            'void WifiSelectionActivity::attemptConnection()',
            'void WifiSelectionActivity::checkConnectionStatus()'))
        cancel=method(source[source.index('// Check connection progress'):],
                      'if (state == WifiSelectionState::CONNECTING || state == WifiSelectionState::AUTO_CONNECTING)')
        program=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define RICKY_PERF(...) ((void)0)
#define tr(id) #id
unsigned long now=0;unsigned long millis(){return now;}void delay(int ms){now+=ms;}
using wl_status_t=int;
constexpr int WIFI_STA=1,WL_IDLE_STATUS=0,WL_CONNECTED=3,WL_CONNECT_FAILED=4,WL_NO_SSID_AVAIL=1;
constexpr int WIFI_ALL_CHANNEL_SCAN=0,WIFI_CONNECT_AP_BY_SIGNAL=0;
struct IPAddress {int operator[](int n)const{return n+1;}};
namespace NetworkStartup {
 bool success=true,associated=false;int starts=0;std::vector<std::string> records;
 bool setMode(int,int){++starts;now+=50000;return success;}
 void recordFailure(const char* stage,int){records.push_back(stage);}
 bool stationAssociated(){return associated;}
}
struct WiFiFake {
 bool radio=true;int disconnects=0;wl_status_t current=WL_IDLE_STATUS,beginResult=WL_IDLE_STATUS;
 void persistent(bool){}void setScanMethod(int){}void setSortMethod(int){}void setHostname(const char*){}
 void disconnect(bool off=false,bool erase=false){++disconnects;radio=!off;NetworkStartup::associated=false;(void)erase;}
 wl_status_t begin(const char*,const char* =nullptr){if(!radio)++NetworkStartup::starts;radio=true;now+=4000;return beginResult;}
 wl_status_t status(){return current;}IPAddress localIP(){return {};}
} WiFi;
bool readStationMac(uint8_t (&)[6]){return false;}
struct {bool clockHasBeenSynced=true;void saveToFile(){}} SETTINGS;
struct {bool isAvailable(){return false;}bool syncFromNTP(){return false;}} halClock;
namespace trustedtime {void startSync(){}}
struct {void setLastConnectedSsid(const std::string&){}} WIFI_STORE;
struct RenderLock {template<class T>RenderLock(T&){} };
struct MappedInputManager {enum class Button{Back,Confirm};bool back=false,confirm=false;bool wasPressed(Button b){return b==Button::Back?back:confirm;}};
enum class WifiSelectionState {CONNECTING,AUTO_CONNECTING,CONNECTION_FAILED,NETWORK_LIST,NETWORK_ERROR,SAVE_PROMPT};
struct WifiSelectionActivity {
 @TIMEOUTS@
 unsigned long connectionStartTime=0;
 int renderer=0,updates=0,autoFailures=0,savePromptSelection=0;
 bool autoConnecting=false,selectedRequiresPassword=true,usedSavedPassword=true,completed=false,result=false;
 WifiSelectionState state=WifiSelectionState::NETWORK_LIST;
 std::string selectedSSID="test",enteredPassword="secret",connectedIP,connectionError;
 MappedInputManager mappedInput;
 void requestUpdate(){++updates;}
 void showNetworkError(){state=WifiSelectionState::NETWORK_ERROR;}
 void handleAutoConnectFailure(){++autoFailures;autoConnecting=false;state=WifiSelectionState::NETWORK_LIST;}
 void showNetworkListFromAutoConnect(){autoConnecting=false;state=WifiSelectionState::NETWORK_LIST;}
 void onComplete(bool ok){completed=true;result=ok;}
 void attemptConnection();void checkConnectionStatus();
 void loopConnecting(){ @CANCEL@ }
};
@METHODS@
int main(){
 using S=WifiSelectionState;
 static_assert(WifiSelectionActivity::CONNECTION_TIMEOUT_MS==30000);
 static_assert(WifiSelectionActivity::AUTO_CONNECTION_TIMEOUT_MS==30000);
 for(bool automatic:{false,true}){
  NetworkStartup::success=true;NetworkStartup::starts=0;NetworkStartup::records.clear();now=0;WiFi={};
  WifiSelectionActivity a;a.autoConnecting=automatic;a.attemptConnection();
  assert(NetworkStartup::starts==1 && WiFi.radio); // never stop/restart newly started STA
  assert(a.connectionStartTime==now && now>30000); // startup cost does not consume join budget
  now+=12000;a.checkConnectionStatus();assert(!a.completed && a.connectionError.empty());
  // A router that takes longer than the former 7-second automatic budget can join.
  WiFi.current=WL_CONNECTED;a.checkConnectionStatus();assert(a.completed && a.result);
 }
 for(bool associated:{false,true}){
  now=0;WiFi={};NetworkStartup::records.clear();WifiSelectionActivity a;a.attemptConnection();
  NetworkStartup::associated=associated;now+=30001;a.checkConnectionStatus();
  assert(a.state==S::CONNECTION_FAILED);
  assert(a.connectionError==(associated?"STR_ERROR_WIFI_IP_TIMEOUT":"STR_ERROR_CONNECTION_TIMEOUT"));
  assert(NetworkStartup::records.back()==(associated?"join-ip-timeout":"join-link-timeout"));
 }
 for(bool automatic:{false,true}){
  WifiSelectionActivity a;a.state=automatic?S::AUTO_CONNECTING:S::CONNECTING;
  a.mappedInput.back=true;a.loopConnecting();assert(a.completed && !a.result);
 }
 NetworkStartup::success=false;WifiSelectionActivity failed;failed.attemptConnection();assert(failed.state==S::NETWORK_ERROR);
 NetworkStartup::success=true;WiFi={};WiFi.current=WL_CONNECT_FAILED;NetworkStartup::records.clear();
 WifiSelectionActivity auth;auth.attemptConnection();auth.checkConnectionStatus();
 assert(auth.state==S::CONNECTION_FAILED && NetworkStartup::records.back()=="join-status");
}
'''.replace('@TIMEOUTS@',timeouts).replace('@METHODS@',methods).replace('@CANCEL@',cancel)
        run_cpp(program,defines=('SIMULATOR_DEVICE_EEGO_A4=1',))
        # Other boards keep their existing connection budgets.
        run_cpp('#include <cstdint>\nstruct T{\n'+timeouts+'};\nint main(){static_assert(T::CONNECTION_TIMEOUT_MS==15000);static_assert(T::AUTO_CONNECTION_TIMEOUT_MS==7000);}')

    def test_association_probe_uses_snapshot_without_disconnect(self):
        source=(ROOT/'src/NetworkStartup.cpp').read_text()
        body=method(source,'bool stationAssociated()')
        run_cpp(r'''
#include <cassert>
constexpr int ESP_OK=0;struct wifi_ap_record_t{};
bool associated=false;int reads=0;
int esp_wifi_sta_get_ap_info(wifi_ap_record_t*){++reads;return associated?ESP_OK:-1;}
'''+body+r'''
int main(){assert(!stationAssociated());associated=true;assert(stationAssociated());assert(reads==2 && associated);}
''')

if __name__=='__main__': unittest.main()
