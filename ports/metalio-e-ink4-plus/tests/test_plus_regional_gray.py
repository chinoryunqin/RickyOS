"""Execute Plus driver and vendor regional submission code with fake panel I/O."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / 'firmware/rickyos-plus-1.2'
driver = (PORT/'components/rickyos_display/src/driver/MetalioPlusDriver.cpp').read_text()
driver = '\n'.join(line for line in driver.splitlines() if not line.startswith('#include'))
high = (PORT/'components/metalio_board/display/highlevel.c').read_text()
high = high[high.index('static enum EpdDrawError update_region('):high.index('\nvoid epd_hl_set_all_white')]
shim = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "PlusFramebuffer.h"
#include "NativeGrayRefreshPolicy.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
constexpr int W=1216,H=684,N=W*H/2;
struct EpdRect {int x,y,width,height;};
enum EpdDrawMode {MODE_GC16=1,MODE_DU=2,MODE_GL16=3};
constexpr int MODE_PACKING_1PPB_DIFFERENCE=16;
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_EMPTY_LINE_QUEUE=1};
struct EpdiyHighlevelState {uint8_t *front_fb,*back_fb,*difference_fb; bool* dirty_lines; uint8_t* dirty_columns; void* waveform;};
uint8_t front[N],back[N],diff[N*2],cols[W/2]; bool lines[H];
EpdiyHighlevelState hl{front,back,diff,lines,cols,nullptr};
struct Call {EpdRect area; bool cleanup;};
std::vector<Call> calls;
int powered=0,off=0,full=0,cleared=0,failDraw=0;
int epd_width(){return W;} int epd_height(){return H;}
EpdRect epd_full_screen(){return {0,0,W,H};}
EpdRect _inverse_rotated_area(int x,int y,int w,int h){return {x,y,w,h};}
EpdRect epd_difference_image_cropped(const uint8_t* to,const uint8_t* from,EpdRect a,uint8_t*,bool* dirty,uint8_t* columns){
 memset(dirty,0,H); memset(columns,0,W/2); bool changed=false;
 for(int y=a.y;y<a.y+a.height;++y)for(int x=0;x<W/2;++x){
  uint8_t d=to[y*W/2+x]^from[y*W/2+x]; if(!d)continue;
  dirty[y]=true; columns[x]|=d;
  if(x*2>=a.x && x*2<a.x+a.width)changed=true;
 }
 return changed?a:EpdRect{};
}
EpdDrawError epd_draw_base(EpdRect,const uint8_t*,EpdRect a,int mode,int,bool*,uint8_t* columns,void*){
 assert(a.x==0 && a.y==0 && a.width==W && a.height==H);
 bool any=false;for(int x=0;x<W/2;++x)any|=columns[x]!=0;assert(any);
 calls.push_back({a,(mode&15)==MODE_GC16});
 if(failDraw && --failDraw==0)return EPD_DRAW_EMPTY_LINE_QUEUE;
 return EPD_DRAW_SUCCESS;
}
'''
backend = r'''
bool MetalioEpd_Init(){memset(front,255,N);memset(back,255,N);return true;}
EpdiyHighlevelState* MetalioEpd_Hl(){return &hl;}
void epd_poweron(){++powered;} void epd_poweroff(){++off;}
bool epd_board_metalio_eink4_plus_tps_ok(){return true;}
EpdDrawError epd_clear_checked(){++cleared;return EPD_DRAW_SUCCESS;}
EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState*,EpdDrawMode,int){++full;memcpy(back,front,N);return EPD_DRAW_SUCCESS;}
EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState*,EpdDrawMode,int){++full;memcpy(back,front,N);return EPD_DRAW_SUCCESS;}
EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* s,EpdDrawMode m,int t){return epd_hl_update_area(s,m,t,epd_full_screen());}
namespace freeink {
struct EpdBus{};
enum class BusyPolarity {ActiveHigh}; enum class OpticalState {Unknown,Bw,Gray}; enum class RefreshMode{Full,Half,Fast};
struct PanelGeometry {uint16_t width,height,widthBytes;uint32_t bufferSize;};
class PanelDriver {
public:
 virtual ~PanelDriver()=default;
 virtual uint32_t spiHz()const=0; virtual BusyPolarity busyPolarity()const=0; virtual PanelGeometry geometry()const=0;
 virtual bool usesExternalBus()const=0;virtual void begin(EpdBus&)=0;virtual void deepSleep(EpdBus&)=0;
 virtual OpticalState opticalState()const=0;
 virtual void display(EpdBus&,const uint8_t*,const uint8_t*,RefreshMode,bool)=0;
 virtual uint8_t* beginGrayscale16()=0;virtual bool commitGrayscale16(const uint8_t*)=0;
 virtual void cancelGrayscale16()=0;virtual void requestResync(uint8_t)=0;
};
}
'''
main = r'''
int main(){
 // Same-row disjoint submissions must not publish the second zone early.
 memset(front,255,N);memset(back,255,N);front[0]=0;front[W/2-1]=0;
 assert(epd_hl_update_area(&hl,MODE_GL16,25,{0,0,32,1})==EPD_DRAW_SUCCESS);
 assert(back[0]==0 && back[W/2-1]==255);
 assert(epd_hl_update_area(&hl,MODE_GL16,25,{W-32,0,32,1})==EPD_DRAW_SUCCESS);
 assert(back[W/2-1]==0);
 front[0]=0x88; failDraw=1;
 assert(epd_hl_update_area_full(&hl,MODE_GC16,25,{0,0,32,1})!=EPD_DRAW_SUCCESS);
 assert(back[0]==0);
 // Actual Plus driver: startup, no-op, regional cleanup, cancellation, recovery.
 using namespace freeink;
 EpdBus bus;auto& d=metalioPlusDriver();d.begin(bus);
 uint8_t proxy[W*H/8];memset(proxy,255,sizeof(proxy));
 assert(!metalioPlusRestoreGray16Baseline(proxy));
 d.beginGrayscale16();front[0]=0;assert(d.commitGrayscale16(proxy));assert(cleared==1);
 int prior=powered;
 metalioPlusSetGray16UiRefresh(W-32,0,32,H,1,4);
 d.beginGrayscale16();memcpy(front,back,N);assert(d.commitGrayscale16(proxy));assert(powered==prior);
 metalioPlusSetGray16UiRefresh(W-32,0,32,H,1,4);
 d.beginGrayscale16();memcpy(front,back,N);front[0]=0x88;front[100]=0;front[W/2-1]=0;
 calls.clear();assert(d.commitGrayscale16(proxy));assert(calls.size()==2);
 assert(calls[0].cleanup && !calls[1].cleanup);assert(back[0]==0x88 && back[W/2-1]==0);
 d.beginGrayscale16();assert(front[0]==255);assert(metalioPlusRestoreGray16Baseline(proxy));assert(front[0]==0x88);
 front[0]=0;metalioPlusSetGray16UiRefresh(0,0,0,0,1,1);d.cancelGrayscale16();assert(front[0]==0x88);
 assert(proxy[0]==0xff); // >=8 gray proxy is white.
 // Failure on second regional scan invalidates the whole optical baseline.
 d.beginGrayscale16();memcpy(front,back,N);front[0]=0;front[100]=0x88;front[W/2-1]=0x88;
 metalioPlusSetGray16UiRefresh(W-32,0,32,H,1,4);failDraw=2;
 assert(!d.commitGrayscale16(proxy));assert(d.opticalState()==OpticalState::Unknown);
 assert(!metalioPlusRestoreGray16Baseline(proxy));
 d.beginGrayscale16();front[1]=0;metalioPlusSetGray16UiRefresh(0,0,0,0,8,32);
 assert(d.commitGrayscale16(proxy));assert(cleared==2);assert(powered==off);
 // New gray is never reduced to DU and ledger uses constant bounded storage.
 static_assert(sizeof(ricky::gray::Ledger)<=64);
}
'''
with tempfile.TemporaryDirectory(prefix='plus-regional-test-') as folder:
    p=Path(folder);(p/'test.cpp').write_text(shim+high+backend+driver+main)
    subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-I',str(PORT/'components/rickyos_display/include'),'-I',str(PORT/'upstream/lib/hal'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Plus actual driver + regional submissions passed (ASan/UBSan)')
