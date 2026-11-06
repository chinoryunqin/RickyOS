"""Exercise production panel geometry, draft actions and cache-free preview dispatch."""
from pathlib import Path
import unittest
import os
from test_reading_ui_regressions import method, run_cpp as compile_cpp
ROOT = Path(os.environ.get('RICKYOS_READER_SOURCE_ROOT', Path(__file__).resolve().parents[2]))
def run_cpp(program):
    compile_cpp(program, include_dirs=(ROOT / 'src/activities/reader', ROOT / 'src/activities', ROOT / 'lib/Epub', ROOT / 'src'))
ACTIVITY = ROOT / 'src/activities/reader/ReaderImageSettingsActivity.cpp'

class ReaderImagePanelTest(unittest.TestCase):
    def test_all_orientations_and_disjoint_controls(self):
        run_cpp(r'''
#include <cassert>
#include <vector>
#include "ReaderImagePanelLayout.h"
using namespace readerImagePanel;
bool intersects(Box a,Box b) {return a.x<b.right() && b.x<a.right() && a.y<b.bottom() && b.y<a.bottom();}
int main(){
 for(auto body : {Box{24,140,636,1040},Box{24,130,1168,520},Box{20,130,500,770},
                   Box{20,120,920,380},Box{16,90,448,680},Box{16,84,768,370}}) {
  for(int caption : {20,30,40}) for(int desired : {44,66}) for(int gap : {8,12}) {
   auto l=layout(body,caption,desired,gap);
   std::vector<Box> controls{l.preview,l.compare,l.next,l.contrast,l.brightness,l.smooth[0],l.smooth[1],l.scope,l.cancel,l.apply};
   for(auto p:l.presets) controls.push_back(p);
   for(auto a:controls) {
    assert(a.width>0 && a.height>0);
    assert(a.x>=body.x && a.y>=body.y && a.right()<=body.right() && a.bottom()<=body.bottom());
   }
   for(size_t i=0;i<controls.size();++i) for(size_t j=i+1;j<controls.size();++j) assert(!intersects(controls[i],controls[j]));
   for(auto tone:{l.contrast,l.brightness}) {
    int width=tone.width-2*(l.control+l.gap);
    assert(width>=l.control); // slider touch target cannot expand into +/-
   }
   assert(l.scope.bottom()+l.gap<=l.cancel.y);
   assert(l.cancel.right()+l.gap<=l.apply.x);
  }
 }
 for(int limit:{40,50}) {
  assert(toneFromPermille(0,limit)==-limit && toneFromPermille(1000,limit)==limit);
  assert(toneFromPermille(500,limit)==0);
  int previous=-limit;
  for(int p=-10;p<=1010;++p) {
   int value=toneFromPermille(p,limit);
   assert(value>=previous && value>=-limit && value<=limit && value%5==0);previous=value;
  }
 }
}
''')

    def test_draft_compare_presets_cancel_and_save_failure(self):
        source=ACTIVITY.read_text()
        functions='\n'.join(method(source,s) for s in (
            'void ReaderImageSettingsActivity::markEdited(', 'void ReaderImageSettingsActivity::setTone(',
            'void ReaderImageSettingsActivity::activate(', 'void ReaderImageSettingsActivity::cancel(',
            'void ReaderImageSettingsActivity::finishWith('))
        run_cpp(r'''
#include <cassert>
#include <algorithm>
#include <functional>
#include <variant>
#include "ReaderImageSettings.h"
#include "ActivityResult.h"
int writes=0;bool saveOk=true;
namespace readerImageSettings {
 ReaderImageProfile defaults(){return {5,10,true};}
 bool save(const std::string&,const ReaderImageDraft&){++writes;return saveOk;}
}
unsigned long millis(){return 123;}
struct Input{};bool buttonsHeld(Input&){return false;}
enum class StrId {STR_READER_IMAGE_SCOPE};
StrId kScopeLabels[3];
struct Popup {std::function<void(int)> callback;void show(StrId,StrId*,int,int,std::function<void(int)> cb){callback=cb;}};
struct App{void clearTapFlash(){}};
struct ReaderImageSettingsActivity {
 enum class Result:int32_t {Preview,Original,Applied};
 enum class Focus:uint8_t {Contrast,Brightness,Smooth,Scope,Original,Photo,Comic,Scan,Compare,Next,Apply,Cancel,Count};
 std::string book_="book.epub";ReaderImageDraft draft_;Popup popup_;Input mappedInput;App app;
 bool initialSmooth_=false,saveFailed_=false,comparing_=false,waitForRelease_=false,previewPending_=false,finished=false;
 int imageCount_=0,imageIndex_=0,loads=0;unsigned long changedAt_=0;Focus focus_=Focus::Contrast;ActivityResult result;
 void requestUpdate(){} void loadImage(){++loads;}void finish(){finished=true;}
 template<class T> void setResult(T&& r){result=ActivityResult(std::forward<T>(r));}
 void markEdited();void setTone(bool,int);void activate(Focus);void cancel();void finishWith(Result);
};
''' + functions + r'''
int main(){
 using A=ReaderImageSettingsActivity;using F=A::Focus;
 A a;a.draft_={{0,0,false},ReaderImageScope::FollowDefault};
 a.setTone(true,100);assert(a.draft_.profile.contrast==50 && a.draft_.scope==ReaderImageScope::Book && writes==0);
 a.setTone(false,-100);assert(a.draft_.profile.brightness==-40);
 auto draft=a.draft_;a.activate(F::Compare);assert(a.comparing_ && a.draft_.profile==draft.profile && writes==0);
 a.activate(F::Compare);assert(!a.comparing_ && a.draft_.profile==draft.profile);
 a.activate(F::Photo);assert((a.draft_.profile==ReaderImageProfile{10,0,true}));
 a.activate(F::Comic);assert((a.draft_.profile==ReaderImageProfile{20,0,true}));
 a.activate(F::Scan);assert((a.draft_.profile==ReaderImageProfile{15,5,true}));
 a.activate(F::Original);assert((a.draft_.profile==ReaderImageProfile{0,15,false}));
 a.activate(F::Scope);a.popup_.callback(2);assert(a.draft_.scope==ReaderImageScope::FollowDefault && a.draft_.profile==readerImageSettings::defaults());
 a.imageCount_=3;a.activate(F::Next);assert(a.imageIndex_==1 && a.loads==1 && writes==0);
 saveOk=false;a.activate(F::Apply);assert(a.saveFailed_ && !a.finished && writes==1);
 saveOk=true;a.activate(F::Apply);assert(a.finished && !a.result.isCancelled && writes==2);
 assert(std::get<IntervalResult>(a.result.data).value==static_cast<int>(A::Result::Applied));
 A b;b.activate(F::Photo);b.cancel();assert(b.finished && b.result.isCancelled && writes==2);
}
''')

    def test_input_barriers_drag_release_and_debounce(self):
        function=method(ACTIVITY.read_text(),'void ReaderImageSettingsActivity::loop(')
        run_cpp(r"""
#include <cassert>
#include <initializer_list>
#include "ReaderImageSettings.h"
unsigned long now=0;unsigned long millis(){return now;}
struct MappedInputManager {
 enum class Button {Back,Confirm,Up,Down,Left,Right};
 bool held=false,back=false,confirm=false;
 bool wasReleased(Button b)const{return (b==Button::Back && back)||(b==Button::Confirm && confirm);}
};
bool buttonsHeld(MappedInputManager& input){return input.held;}
struct RenderLock {template<class T> explicit RenderLock(T&) {}};
struct Popup {template<class F> bool handleInput(MappedInputManager&,F){return false;}};
struct Navigator {template<class F> void onPressAndContinuous(std::initializer_list<MappedInputManager::Button>,F){}};
struct ReaderImageSettingsActivity {
 enum class Focus:uint8_t {Contrast,Brightness,Smooth,Scope,Original,Photo,Comic,Scan,Compare,Next,Apply,Cancel,Count};
 struct Route {struct Snap{bool touchHeld=false,touchPressed=false;}snap;bool action=false;explicit operator bool()const{return action;}};
 Route inputRoute;MappedInputManager mappedInput;Popup popup_;Navigator navigator_;
 ReaderImageDraft draft_;Focus focus_=Focus::Contrast;
 bool waitForRelease_=false,dragging_=false,previewPending_=false,showFocus_=false;
 unsigned long changedAt_=0;int updates=0,cancels=0,activations=0;
 Route routeTouch(MappedInputManager&,bool,bool){return inputRoute;}
 void requestUpdate(){++updates;}void cancel(){++cancels;}void activate(Focus){++activations;}
 void setTone(bool,int){}void markEdited(){}void loop();
};
"""+function+r"""
int main(){
 ReaderImageSettingsActivity a;
 a.waitForRelease_=true;a.mappedInput.held=true;a.mappedInput.confirm=true;a.loop();assert(a.activations==0);
 a.mappedInput.held=false;a.loop();assert(!a.waitForRelease_ && a.activations==0); // release consumed
 a.mappedInput.confirm=false;a.loop();assert(a.activations==0);
 a.mappedInput.confirm=true;a.loop();assert(a.activations==1);
 a.mappedInput.confirm=false;a.dragging_=true;a.previewPending_=true;
 a.inputRoute.snap.touchHeld=true;a.mappedInput.back=true;now=100;a.loop();assert(a.cancels==0 && a.updates==0);
 a.inputRoute.snap.touchHeld=false;a.loop();assert(!a.dragging_ && a.cancels==0 && a.changedAt_==100);
 a.mappedInput.back=false;now=399;a.loop();assert(a.updates==0);
 now=400;a.loop();assert(a.updates==1 && !a.previewPending_);
 now=900;a.loop();assert(a.updates==1);
 a.mappedInput.back=true;a.loop();assert(a.cancels==1);
}
""")

    def test_preview_dispatch_does_not_write_pixel_cache(self):
        source=(ROOT/'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
        function=method(source,'bool ImageBlock::renderPreview(')
        run_cpp(r'''
#include <cassert>
#include <string>
struct Cancel {bool stopped=false;bool isCancelled()const{return stopped;}};
struct RenderConfig {Cancel cancellation;std::string cachePath;int tone=20;};
struct GfxRenderer{};
int extracted=0,decoded=0;bool extractOk=true,decodeOk=true;
struct Decoder {bool decodeToFramebuffer(const std::string&,GfxRenderer&,const RenderConfig& config){++decoded;assert(config.cachePath.empty() && config.tone==20);return decodeOk;}} decoder;
struct ImageDecoderFactory {static Decoder* getDecoder(const std::string&){return &decoder;}};
struct ImageBlock {std::string imagePath="source.jpg";bool ensureExtracted(Cancel){++extracted;return extractOk;}bool renderPreview(GfxRenderer&,const RenderConfig&);};
''' + function + r'''
int main(){GfxRenderer renderer;ImageBlock block;RenderConfig config;
 assert(block.renderPreview(renderer,config) && extracted==1 && decoded==1);
 config.cancellation.stopped=true;assert(!block.renderPreview(renderer,config) && extracted==1 && decoded==1);
 config.cancellation.stopped=false;extractOk=false;assert(!block.renderPreview(renderer,config) && decoded==1);
 extractOk=true;decodeOk=false;assert(!block.renderPreview(renderer,config));
}
''')
if __name__=='__main__':unittest.main()
