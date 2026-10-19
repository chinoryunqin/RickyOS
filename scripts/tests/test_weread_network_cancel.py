"""Build the real HTTP implementation against a deterministic stalled TLS transport."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NetworkCancelTest(unittest.TestCase):
    def test_stalls_cancel_and_long_progressing_downloads_complete(self):
        with tempfile.TemporaryDirectory(prefix='weread-cancel-') as d:
            d=Path(d)
            (d/'Arduino.h').write_text(r'''
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
extern unsigned long clockMs;
inline unsigned long millis(){return clockMs;}
inline void delay(int ms){clockMs+=ms;}
struct ESPType {unsigned getFreeHeap(){return 100000;} unsigned getMaxAllocHeap(){return 100000;}};
inline ESPType ESP;
inline unsigned uxTaskGetStackHighWaterMark(void*){return 10000;}
''')
            (d/'Logging.h').write_text('#pragma once\n#define LOG_DBG(...) (void)0\n#define LOG_INF(...) (void)0\n#define LOG_ERR(...) (void)0\n')
            (d/'WiFi.h').write_text('#pragma once\nusing wifi_mode_t=int; constexpr int WIFI_MODE_STA=1,WL_CONNECTED=1;\nstruct W {int getMode(){return 1;}int status(){return 1;}};inline W WiFi;\n')
            (d/'SecureClient.h').write_text(r'''
#pragma once
#include <Arduino.h>
#include <functional>
#include <string>
extern int scenario;
extern int connectTimeout;
namespace freeink {
class SecureClient {
 bool live=false; unsigned long timeout=0;size_t cursor=0;
 std::function<bool()> abort;
 std::string reply; unsigned slowChunks=0;
 public:
 void setInsecure(){}
 void setTimeout(unsigned long ms){timeout=ms;}
 void setAbortCallback(std::function<bool()> p){abort=p;}
 bool connected(){return live;}
 void stop(){live=false;}
 int available(){return 0;}
 bool connect(const char*,int){
   connectTimeout=timeout;
   if(scenario==5){clockMs+=4000;if(timeout<4000)return false;}
   if(scenario==0) {for(int n=0;n<1000;++n){clockMs+=10;if(abort && abort())return false;}}
   live=true;
   reply=scenario==7?"HTTP/1.1 200 OK\r\nContent-Length: 75\r\n\r\n":scenario==1?"":scenario==2?"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n":(scenario==3 || scenario==6)?"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n":"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
   return true;
 }
 size_t write(const uint8_t*,size_t n){return n;}
 int read(){++clockMs;return cursor<reply.size()?uint8_t(reply[cursor++]):-1;}
 int read(uint8_t* b,size_t n){
   ++clockMs;
   if(cursor<reply.size()){size_t k=std::min(n,reply.size()-cursor);memcpy(b,reply.data()+cursor,k);cursor+=k;return int(k);}
   if(scenario==3){if(slowChunks++<750){b[0]='x';clockMs+=100;return 1;} live=false;return 0;}
   if(scenario==7){if(slowChunks++<75){b[0]='x';clockMs+=1000;return 1;} live=false;return 0;}
   if(scenario==6){if(slowChunks++<25){b[0]='x';clockMs+=1000;return 1;} live=false;return 0;}
   return 0;
 }
};
}
''')
            (d/'check.cpp').write_text(r'''
#include "WeReadHttpClient.h"
#include <cassert>
unsigned long clockMs=0;
int scenario=0,connectTimeout=0;
bool cancel(void*){return clockMs>=80;}
int main(){
 using namespace WeReadHttpClient;
 uint8_t buffer[256];RequestOptions options;options.readBuffer=buffer;options.readBufferSize=sizeof buffer;options.timeoutMs=500;
 for(scenario=0;scenario<4;++scenario){
   clockMs=0;int status=-1;Session session;AbortScope scope(cancel,nullptr);
   auto result=request(session,"https://weread.qq.com/test",options,{}, {},status);
   assert(result==Result::Aborted && clockMs<200);
   assert(connectTimeout==options.timeoutMs && !session.reusable());
 }
 // A genuinely stalled body still fails using the original idle timeout.
 scenario=2;clockMs=0;int status=-1;
 assert(request("https://weread.qq.com/test",options,{}, {},status)==Result::NetworkError);
 assert(clockMs>=500 && clockMs<700);
 // Receiving data for 75 seconds is healthy, including Content-Length chapters.
 options.timeoutMs=20000;
 for(int mode:{3,7}) {
   scenario=mode;clockMs=0;std::string received;
   auto result=request("https://weread.qq.com/test",options,[&](const uint8_t* b,size_t n){received.append((const char*)b,n);return true;},{},status);
   assert(result==Result::Ok && status==200 && clockMs>=75000 && clockMs<77000);
   assert(received.size()==(mode==3?750:75));
 }
 // A healthy 4-second handshake must retain the original 20-second allowance.
 options.timeoutMs=20000;scenario=5;clockMs=0;
 assert(request("https://weread.qq.com/test",options,{}, {},status)==Result::Ok);
 assert(connectTimeout==20000 && clockMs>=4000);
 // A healthy progressing response taking 25 seconds must also complete.
 scenario=6;clockMs=0;
 assert(request("https://weread.qq.com/test",options,{}, {},status)==Result::Ok);
 assert(clockMs>=25000 && clockMs<27000);
 // A later success does not inherit cancellation or timeout state.
 scenario=4;clockMs=0;std::string output;
 auto result=request("https://weread.qq.com/test",options,[&](const uint8_t* b,size_t n){output.append((const char*)b,n);return true;},{},status);
 assert(result==Result::Ok && output=="ok" && status==200);
}
''')
            exe=d/'check'
            subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-variable',
                            '-DFREEINK_NET_WOLFSSL=1','-DCROSSPOINT_VERSION="test"',
                            '-fsanitize=address,undefined','-I'+str(d),
                            '-I'+str(ROOT/'lib/WeReadWebApi/src'),str(d/'check.cpp'),
                            str(ROOT/'lib/WeReadWebApi/src/WeReadHttpClient.cpp'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


class ActivityCancellationTest(unittest.TestCase):
    def test_job_timer_starts_after_wifi_and_abort_only_unwinds(self):
        from test_reading_ui_regressions import method, run_cpp
        source=(ROOT/'src/activities/apps/weread/webapi/WeReadActivity.cpp').read_text()
        start=method(source,'void WeReadActivity::startJob(')
        self.assertIn('jobStartedAt_ = millis();',start)
        for flag in ('jobBackRequested_', 'jobHomeRequested_', 'jobTimedOut_'):
            self.assertIn(flag+' = false;',start)
        probe=method(source,'bool WeReadActivity::jobAbortProbe(')
        self.assertNotIn('activityManager',probe)
        self.assertNotIn('operation_.reset()',probe)
        advance=method(source,'void WeReadActivity::advanceJob()')
        self.assertLess(advance.index('event = stepOperation();'),advance.index('operation_.reset();'))
        self.assertIn('loadSelectedDetail();',advance)
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
unsigned long now=0;unsigned long millis(){return now;}
struct MappedInputManager {
 enum class Button{Back};bool back=false,home=false;int polls=0;
 void update(bool deferred){assert(deferred);++polls;}
 bool wasReleased(Button){return back;}bool wasHomeGesture(){return home;}
};
struct WeReadActivity {
 enum class Job{Detail,Sync,Download};
 MappedInputManager mappedInput;
 bool jobBackRequested_=false,jobHomeRequested_=false,jobTimedOut_=false;
 Job retryJob_=Job::Detail;uint32_t jobStartedAt_=100;
 static constexpr uint32_t kDetailWaitMs=60000;
 struct Op{bool cancelled=false;void cancel(){cancelled=true;}} operation_;
 static bool jobAbortProbe(void*);
};
'''+probe+r'''
int main(){
 for(int mode:{0,1,2,3}){
  WeReadActivity a;now=100;
  assert(!a.jobAbortProbe(&a) && !a.operation_.cancelled);
  if(mode==0)a.mappedInput.back=true;
  if(mode==1)a.mappedInput.home=true;
  if(mode==2)now=60100;
  if(mode==3){a.retryJob_=WeReadActivity::Job::Download;now=100000;}
  assert(a.jobAbortProbe(&a)==(mode!=3));
  assert(a.operation_.cancelled==(mode!=3));
  a.mappedInput.back=a.mappedInput.home=false;
  assert(a.jobAbortProbe(&a)==(mode!=3)); // input persists through unwind
 }
}
''')

    def test_startup_failures_do_not_all_claim_network_error(self):
        from test_reading_ui_regressions import method, run_cpp
        source=(ROOT/'src/activities/apps/weread/webapi/WeReadActivity.cpp').read_text()
        error_message=method(source,'const char* WeReadActivity::errorMessage() const')
        run_cpp(r'''
#include <cassert>
#include <utility>
#include <initializer_list>
#include <cstring>
#define tr(id) #id
constexpr int WL_CONNECTED=1;
struct {int connected=1;int status(){return connected;}} WiFi;
namespace WeReadClient {enum class Error{Ok,Network,Clock,Protocol,Integrity,SessionExpired,LoginFailed,SdCard,OutOfMemory,Unavailable,WholeBookOnly};}
struct WeReadActivity {WeReadClient::Error error_;const char* errorMessage()const;};
'''+error_message+r'''
int main(){
 using Error=WeReadClient::Error;
 for(auto pair:{std::pair{Error::Clock,"STR_WEREAD_CLOCK_ERROR"},
                std::pair{Error::Protocol,"STR_WEREAD_RESPONSE_ERROR"},
                std::pair{Error::Integrity,"STR_WEREAD_INTEGRITY_ERROR"},
                std::pair{Error::SessionExpired,"STR_WEREAD_LOGIN_REQUIRED"},
                std::pair{Error::LoginFailed,"STR_WEREAD_LOGIN_REQUIRED"},
                std::pair{Error::Network,"STR_WEREAD_HTTP_ERROR"}}){
   WeReadActivity a{pair.first};assert(strcmp(a.errorMessage(),pair.second)==0);
 }
 WiFi.connected=0;WeReadActivity a{Error::Network};
 assert(strcmp(a.errorMessage(),"STR_WEREAD_NO_WIFI")==0);
}
''')

if __name__=='__main__': unittest.main()
