"""Run the reader's actual refresh decisions and whole-plane image pass on A4 and peers."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ReaderImageRefreshTest(unittest.TestCase):
    def test_image_refresh_keeps_the_complete_frame(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        policy = source[source.index('  const bool manualRefreshPending = forcedRefreshPending;'):source.index('  const auto drawGuideLines =')]
        base = source[source.index('  if (absoluteImageGrayscale) {\n    const auto baseMode'):source.index('  const auto tDisplay = millis();')]
        planes = source[source.index('    if (needsAnyGrayscale) {\n      if (!renderer.storeBwBuffer())'):source.index('\nvoid EpubReaderActivity::renderStatusBar()')]
        # Drop only renderContents' closing brace; keep the production image-pass body.
        planes = planes.rsplit('}', 1)[0]
        stub = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
namespace HalDisplay {
enum RefreshMode { FAST_REFRESH, HALF_REFRESH, FULL_REFRESH };
enum class GrayscaleMode { Overlay, Absolute };
enum class Controller { UC8279, Other };
}
enum class DisplayRefreshContext { ImageReading };
struct { bool textAntiAliasing=false; } SETTINGS;
unsigned long millis(){return 1;}
template<class... T> void log(const char*,T...){}
#define LOG_ERR(...) log(__VA_ARGS__)
#define LOG_DBG(...) log(__VA_ARGS__)
#define RICKY_PERF(...) log(__VA_ARGS__)
struct Renderer {
 enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
 bool inverted=false; unsigned bw=0,gray=0,stored=0,lsb=0,msb=0;
 int frame=0xa5,glass=0,snapshot=0; RenderMode mode=BW;
 HalDisplay::RefreshMode lastMode=HalDisplay::FAST_REFRESH;
 bool isInverted() const {return inverted;}
 int getGrayscaleLevels()const{return 4;}
 struct Caps { bool stripUploads=false;bool supported()const{return false;} };
 Caps grayscaleCapabilities(HalDisplay::GrayscaleMode)const{return {};}
 bool supportsTextOnlyCombinedBase()const{return false;}
 bool supportsAsyncRefresh()const{return false;}
 bool supportsContinuousImageReading()const{return false;}
 bool displayGrayscaleBase(HalDisplay::GrayscaleMode,HalDisplay::RefreshMode){return false;}
 void displayBuffer(HalDisplay::RefreshMode m,DisplayRefreshContext){++bw;lastMode=m;glass=frame;}
 bool storeBwBuffer(){++stored;snapshot=frame;return true;}
 void clearScreen(int fill){frame=fill;}
 void setRenderMode(RenderMode m){mode=m;}
 void copyGrayscaleLsbBuffers(){++lsb;}
 void copyGrayscaleMsbBuffers(){++msb;}
 void displayGrayBuffer(){++gray;glass=0x11;}
 void restoreBwBuffer(){frame=snapshot;}
 void cancelGrayscale(){}
};
using GfxRenderer=Renderer;
struct {bool deviceIsX3(){return false;}} gpio;
struct {HalDisplay::Controller getController(){return HalDisplay::Controller::UC8279;}} display;
struct {bool isSwitchPending(){return false;}} activityManager;
struct Page {bool imagesAreAllJpeg()const{return true;}} object;
// The stub panel has 4 gray levels, so the native 16-level page path never applies.
bool nativeGray16Page(const Page&){return false;}
namespace ReaderUtils {
HalDisplay::RefreshMode consumeRefreshMode(int& debt){if(debt<=1){debt=5;return HalDisplay::HALF_REFRESH;}--debt;return HalDisplay::FAST_REFRESH;}
void displayWithRefreshCycle(Renderer& r,int& debt,bool=false){r.displayBuffer(consumeRefreshMode(debt),DisplayRefreshContext::ImageReading);}
void displayBaseWithRefreshCycle(Renderer& r,int& debt,bool){displayWithRefreshCycle(r,debt);}
}
void render(Renderer& renderer,bool pageHasImages,int& pagesUntilFullRefresh,bool forcedRefreshPending) {
 Page* page=&object;
 constexpr unsigned long t0=0,tPrewarm=0,tBwRender=0,tDisplay=0;
 constexpr bool pageCacheHit=false; constexpr unsigned long cacheBaseMs=0;
 auto renderGrayscalePass=[&]{renderer.frame=0x33;};
"""
        main = r"""
}
int main(){
 for(bool aa:{false,true})for(bool inverted:{false,true})for(bool images:{false,true})for(bool manual:{false,true})for(int debt:{1,4}){
  SETTINGS.textAntiAliasing=aa; Renderer r;r.inverted=inverted;int remaining=debt;
  render(r,images,remaining,manual);
#if EXPECT_A4
  assert(r.bw==1 && r.gray==0 && r.stored==0 && r.lsb==0 && r.msb==0);
  assert(r.glass==0xa5 && r.frame==0xa5);
  if(images){assert(r.lastMode==HalDisplay::HALF_REFRESH && remaining==1);}
  else {assert(r.lastMode==(debt==1?HalDisplay::HALF_REFRESH:HalDisplay::FAST_REFRESH));}
#else
  bool grayscale=!inverted && (aa || images);
  assert(r.bw==1 && r.gray==unsigned(grayscale));
  assert(r.stored==unsigned(grayscale));
  assert(r.frame==0xa5);
  if(images)assert(r.lastMode==((manual||debt==1)?HalDisplay::HALF_REFRESH:HalDisplay::FAST_REFRESH));
#endif
 }
}
"""
        with tempfile.TemporaryDirectory(prefix='a4-reader-images-') as directory:
            cpp=Path(directory)/'check.cpp'; exe=Path(directory)/'check'
            cpp.write_text(stub+policy+base+'{\n'+planes+main)
            for defines in [('FREEINK_DEVICE_EEGO_A4=1','EXPECT_A4=1'),
                            ('SIMULATOR_DEVICE_EEGO_A4=1','EXPECT_A4=1'),
                            ('FREEINK_DEVICE_READPICO=1','EXPECT_A4=0'),
                            ('FREEINK_DEVICE_METALIO_PLUS=1','EXPECT_A4=0')]:
                subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                                *('-D'+d for d in defines),str(cpp),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
