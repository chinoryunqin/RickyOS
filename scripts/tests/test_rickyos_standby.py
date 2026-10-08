"""Exercise product boot and retained-page drawing; guard wallpaper navigation."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class RickyStandbyTest(unittest.TestCase):
    def test_static_boot_product_and_stock(self):
        splash = method((ROOT / 'src/activities/boot_sleep/BootActivity.cpp').read_text(),
                        'void BootActivity::renderSplash(')
        program = r'''
#include <cassert>
#include <cstring>
constexpr const char* CROSSPOINT_VERSION="test-version";
constexpr int STR_BOOTING=0;
const char* tr(int) {return "Booting";}
struct HalDisplay {enum {FULL_REFRESH=1};};
struct Renderer {int paints=0,mode=0;void displayBuffer(int m=0){++paints;mode=m;}};
struct Theme {
 int staticMarks=0,stockMarks=0;
 void drawRickyPowerScreen(Renderer&,bool asleep,bool transition){assert(!asleep&&!transition);++staticMarks;}
 void drawSplash(Renderer&,const char*,const char* version){assert(!strcmp(version,CROSSPOINT_VERSION));++stockMarks;}
} GUI;
struct BootActivity {Renderer renderer;void renderSplash();};
''' + splash + r'''
int main(){BootActivity boot;boot.renderSplash();assert(boot.renderer.paints==1);
#ifdef RICKYOS_PRODUCT
 assert(GUI.staticMarks==1&&GUI.stockMarks==0&&boot.renderer.mode==HalDisplay::FULL_REFRESH);
#else
 assert(GUI.staticMarks==0&&GUI.stockMarks==1);
#endif
}
'''
        run_cpp(program, defines=('RICKYOS_PRODUCT',))
        run_cpp(program)

    def test_badge_only_changes_its_small_safe_rectangle(self):
        badge = method((ROOT / 'src/components/themes/BaseTheme.cpp').read_text(),
                       'Rect BaseTheme::drawRickyStandbyIndicator(')
        program = r'''
#include <algorithm>
#include <cassert>
#include <vector>
#include <cstring>
#include "components/Rect.h"
constexpr int SMALL_FONT_ID=0,STR_RICKY_STANDBY=0,STR_RICKY_POWERED_OFF=1;
const char* tr(int id){return id?"Off":"Standby";}
struct GfxRenderer {
 Rect safe;int screenWidth,screenHeight;
 mutable std::vector<int> pixels;
 mutable Rect badge{};
 struct ClipScope{ClipScope(const GfxRenderer&,int,int,int,int){}};
 int getTextWidth(int,const char*)const{return 112;}
 int getLineHeight(int)const{return 28;}
 void fillRect(int x,int y,int w,int h,bool)const{
   badge=Rect{x,y,w,h};
   for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx){
     assert(xx>=safe.x&&yy>=safe.y&&xx<safe.x+safe.width&&yy<safe.y+safe.height);
     pixels[yy*screenWidth+xx]=0;
   }
 }
 void drawRect(int x,int y,int w,int h)const{assert(x==badge.x&&y==badge.y&&w==badge.width&&h==badge.height);}
};
struct UITheme {
 static UITheme& getInstance(){static UITheme value;return value;}
 Rect getScreenSafeArea(const GfxRenderer& renderer){return renderer.safe;}
 static void drawCenteredWrappedText(const GfxRenderer& r,Rect rect,int,const char* label,int lines){
   assert(!strcmp(label,"Standby")&&lines==1&&rect.x==r.badge.x&&rect.y==r.badge.y);
 }
};
struct BaseTheme{static Rect drawRickyStandbyIndicator(const GfxRenderer&,bool=false);};
''' + badge + r'''
int main(){
 for(Rect safe : {Rect{32,32,620,1152},Rect{32,32,1152,620},Rect{12,20,456,760},Rect{20,12,760,456}}){
   GfxRenderer renderer{safe,safe.x+safe.width+32,safe.y+safe.height+32,{}, Rect{}};
   renderer.pixels.assign(renderer.screenWidth*renderer.screenHeight,7);
   BaseTheme::drawRickyStandbyIndicator(renderer);
   assert(renderer.badge.height <= 48);
   assert(renderer.badge.width*renderer.badge.height < safe.width*safe.height/20);
   for(int y=0;y<renderer.screenHeight;++y)for(int x=0;x<renderer.screenWidth;++x){
     Rect b=renderer.badge;
     bool inside=x>=b.x&&y>=b.y&&x<b.x+b.width&&y<b.y+b.height;
     assert(renderer.pixels[y*renderer.screenWidth+x]==(inside?0:7));
   }
 }
}
'''
        run_cpp(program, include_dirs=(ROOT / 'src',))
        run_cpp(program, include_dirs=(ROOT / 'src',), defines=('CROSSMUX_UI_PROFILE_HIGH_DPI',))

    def test_retained_reader_orientation_restored_without_clear(self):
        draw = method((ROOT / 'src/activities/boot_sleep/SleepActivity.cpp').read_text(),
                      'void SleepActivity::renderLastScreenSleepScreen(')
        program = r'''
#include <cassert>
#include <initializer_list>
struct HalDisplay{enum {FAST_REFRESH=1};};
struct GfxRenderer{
 int orientation=0;
 mutable int paints=0,gray=0;
 int getOrientation()const{return orientation;}
 void setOrientation(int o){orientation=o;}
 void displayBuffer(int mode)const{assert(mode==1);++paints;}
 void displayGrayscaleBase(int mode)const{assert(mode==1);++gray;}
};
struct State{bool lastSleepFromReader=true;} APP_STATE;
struct Settings{int orientation=0;} SETTINGS;
struct ReaderUtils{static void applyOrientation(GfxRenderer& r,int o){r.setOrientation(o);}};
struct Theme{int calls=0,lastOrientation=-1;void drawRickyStandbyIndicator(GfxRenderer& r,bool off){assert(off);++calls;lastOrientation=r.orientation;}} GUI;
struct Gpio{bool x3=false;bool deviceIsX3()const{return x3;}} gpio;
struct SleepActivity{GfxRenderer& renderer;void renderLastScreenSleepScreen()const;};
''' + draw + r'''
int main(){
 for(bool reader:{false,true})for(int orientation:{0,1,2,3})for(bool x3:{false,true}){
   APP_STATE.lastSleepFromReader=reader;SETTINGS.orientation=orientation;gpio.x3=x3;
   GfxRenderer renderer;SleepActivity sleep{renderer};sleep.renderLastScreenSleepScreen();
   assert(GUI.lastOrientation==(reader?orientation:0));assert(renderer.orientation==0);
   assert(renderer.paints==(x3?0:1)&&renderer.gray==(x3?1:0));
 }
}
'''
        run_cpp(program, defines=('RICKYOS_PRODUCT',))

    def test_wallpaper_picker_preview_cancel_and_decode_guard(self):
        # Picking lives on the Apps → Standby page; Settings links there.
        settings = (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text()
        self.assertIn('SettingAction::RickyStandbyPage', settings)
        self.assertIn('startActivityForResultWith<RickyStandbySettingsActivity>', settings)
        page = (ROOT / 'src/activities/apps/standby/RickyStandbySettingsActivity.cpp').read_text()
        self.assertIn('FileBrowserActivity::Mode::PickWallpaper', page)
        self.assertIn('startActivityForResultWith<ImageViewerActivity>(previewDone, entry->path, true)', page)
        viewer = (ROOT / 'src/activities/util/ImageViewerActivity.cpp').read_text()
        on_enter = method(viewer, 'void ImageViewerActivity::onEnter(')
        self.assertIn('imageReady = false', on_enter)
        preview = method(viewer, 'bool ImageViewerActivity::preparePreview(')
        self.assertIn('preparePreview()', on_enter)
        self.assertIn('FrameBufferLoan loan(renderer)', preview)
        self.assertIn('JpegToBmpConverter::Output::Gray8', preview)
        self.assertIn('constexpr bool absolute = false', on_enter)
        self.assertIn('if (!wallpaperPicker && siblingImages.empty()', on_enter)
        self.assertIn('if (wallpaperPicker)', method(viewer, 'void ImageViewerActivity::loop('))
        options = method(viewer, 'void ImageViewerActivity::showSleepCoverOptions(')
        self.assertLess(options.index('if (!imageReady) return'), options.index('doSetSleepCover('))
        self.assertIn('FsHelpers::hasJpgExtension(filePath) ? previewPath.c_str()', options)
        install = method(viewer, 'bool ImageViewerActivity::doSetSleepCover(')
        self.assertIn('copied == expected', install)
        self.assertIn('if (!SETTINGS.saveToFile())', install)
        self.assertIn('SETTINGS.sleepScreen = previousMode', install)
        self.assertIn('Storage.rename(SLEEP_IMAGE_BACKUP_PATH, SLEEP_IMAGE_PATH)', install)

    def test_sleep_network_toasts_do_not_damage_retained_page(self):
        source = (ROOT / 'src/main.cpp').read_text()
        drain = method(source, 'static void deliverSleepPluginEvents(')
        self.assertEqual(drain.count('pluginevents::drain(preserveFrame ? nullptr : &renderer)'), 2)
        self.assertIn('if (!preserveFrame) GUI.drawPopup', drain)
        enter = method(source, 'void enterDeepSleep(')
        self.assertIn('deliverSleepPluginEvents(isQuickResumeSleep)', enter)


if __name__ == '__main__':
    unittest.main()
