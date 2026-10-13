"""Render real control-center methods with real FUI to audit all hit rectangles."""
from pathlib import Path
import subprocess,tempfile,re
ROOT=Path(__file__).resolve().parents[1];u=ROOT/'firmware/rickyos-plus-1.2/upstream'
s=(u/'src/activities/util/FrontlightPanelActivity.cpp').read_text()
methods=[]
for signature in ['void FrontlightPanelActivity::addSliderRow(', 'int FrontlightPanelActivity::rickyPanelHeight() const','void FrontlightPanelActivity::roundActionRow(','void FrontlightPanelActivity::buildRickyPanel(']:
 a=s.index(signature);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 methods.append(s[a:b])
ids=sorted(set(re.findall(r'(?:StrId::)?(STR_[A-Z0-9_]+)', ''.join(methods))))
code=r'''
#include <FreeInkApp.h>
#include <FreeInkUIIcon.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iterator>
namespace fui=freeink::ui;
using UiScreen=fui::Screen<24>;
enum class StrId {IDS};
IDS_CONST
const char* tr(StrId){return "test";}
struct {const char* get(StrId){return "test";}} I18N;
struct {bool available=true;bool present()const{return available;}} audio;
namespace HalAudioOutput {bool present(){return audio.available;}}
struct {bool available=true,dual=true;bool present()const{return available;}bool hasColorTemperature()const{return dual;}} Frontlight;
struct {int orientation=0,refreshFrequency=0,screenInverted=0;} SETTINGS;
struct Target: fui::DrawTarget {
 fui::Size measureText(fui::FontId f,const char* t,fui::TextStyle)const override{return {static_cast<int16_t>(strlen(t)*10),lineHeight(f)};}
 int16_t lineHeight(fui::FontId f)const override{return f==2?44:28;}
 void fill(fui::Rect,fui::Paint,uint8_t=0,uint8_t=fui::CornersAll)override{}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t=0,uint8_t=fui::CornersAll)override{}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
 void text(fui::Rect,const char*,fui::TextStyle)override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint=fui::Paint::solid(fui::Color::Black),fui::Rotation=fui::Rotation::None)override{}
};
fui::ThemeTokens tokens(){fui::ThemeTokens t;t.smallText.font=1;t.titleText.font=2;t.minTouchSize=88;t.sheetRadius=24;return t;}
fui::ThemeTokens uiThemeTokens(const Target&,bool){return tokens();}
struct {int topPadding=0;} metrics;
auto& uiThemeMetrics(bool){return metrics;}
struct Renderer {int width=684,height=1216;int getScreenWidth()const{return width;}int getScreenHeight()const{return height;}};
struct Rect {int x,y,width,height;};
struct {void drawHeaderWithStyle(Renderer&,Rect,void*,void*,bool,bool){}} GUI;
uint8_t iconBits[200]{};
const freeink::Icon icon_ricky_night_40{40,40,20,iconBits},icon_ricky_refresh_40{40,40,20,iconBits},
 icon_ricky_standby_40{40,40,20,iconBits},icon_ricky_transfer_40{40,40,20,iconBits},
 icon_ricky_portrait_40{40,40,20,iconBits},icon_ricky_landscape_40{40,40,20,iconBits},icon_ricky_clean_40{40,40,20,iconBits};
const freeink::Icon icon_sun_filled_32{32,32,16,iconBits},icon_sun_32{32,32,16,iconBits},kPlayIcon{32,32,16,iconBits};
constexpr int ACTION_BRIGHTNESS=1,ACTION_WARMTH=2,ACTION_TOGGLE=3,ACTION_BRIGHTNESS_STEP=4,ACTION_WARMTH_STEP=5,ACTION_TILE=6;
constexpr int BRIGHTNESS_STEP=1,kVolumeStep=101,kVolumePreview=200,kPanelClose=202,kActionDiameter=88,kActionLabelGap=8;
constexpr int kRefreshPages[]={1,5,10,15,30,0};
class FrontlightPanelActivity {public:
 enum class Tile:uint8_t {Night,Refresh,Standby,Transfer,Orientation,Clean,Light,Preview=200};
 struct RoundAction {Tile tile;const freeink::Icon* icon;const char* label;bool on;};
 Renderer renderer;Target uiTarget;
 struct {bool hasTouch()const{return true;}} mappedInput;
 uint8_t brightness=60,warmth=50,volume=35;bool lightOn=false,overReader=false;int panelBottom=0;char refreshText[24];
 fui::SliderRowProps rowProps;
 int rickyPanelHeight()const;
 void addSliderRow(UiScreen&,const char*,uint8_t,fui::ActionId,fui::ActionId,bool);
 void roundActionRow(UiScreen&,const RoundAction*,int);
 void buildRickyPanel(UiScreen&);
};
'''.replace('IDS',','.join(ids)).replace('IDS_CONST','')
# Correct the placeholder that contains IDS itself before replacement.
code=code.replace(','.join(ids)+'_CONST','\n'.join('constexpr auto '+i+'=StrId::'+i+';' for i in ids))
code+='\n'.join(methods)+r'''
int main(){
 for(bool portrait:{true,false})for(bool reader:{true,false})for(bool light:{true,false})for(bool sound:{true,false})for(bool dual:{true,false}){
  Frontlight.available=light;Frontlight.dual=dual;audio.available=sound;
  FrontlightPanelActivity p;p.renderer=portrait?Renderer{684,1216}:Renderer{1216,684};p.overReader=reader;p.lightOn=true;p.panelBottom=p.rickyPanelHeight();
  assert(p.panelBottom<=p.renderer.height-16);
  fui::DeviceContext device;device.width=p.renderer.width;device.height=p.renderer.height;device.hasTouch=true;device.safeArea={8,8,8,8};
  fui::InputSnapshot input;fui::InteractionBuffer<24> hits;fui::Frame<24> frame(p.uiTarget,device,input,hits);auto theme=tokens();UiScreen screen(frame,theme);
  p.buildRickyPanel(screen);assert(!hits.overflowed());
  int sliders=0,close=0;
  for(size_t i=0;i<hits.count();++i){auto a=hits.data()[i];assert(a.rect.width>=88&&a.rect.height>=88);assert(a.rect.y>=0&&a.rect.bottom()<=p.panelBottom);
   if(a.inputMask & fui::InputDrag)++sliders;
   if(a.action==ACTION_TILE&&a.value==kPanelClose)++close;
   for(size_t j=0;j<i;++j){auto b=hits.data()[j];
    assert(a.rect.right()+8<=b.rect.x||b.rect.right()+8<=a.rect.x||a.rect.bottom()+8<=b.rect.y||b.rect.bottom()+8<=a.rect.y);
   }
  }
  assert(sliders==(light?(dual?2:1):0)+(sound?1:0));assert(close==1);
 }
}
'''
with tempfile.TemporaryDirectory(prefix='plus-cc-layout-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(code)
 sdk=u/'freeink-sdk/libs'
 subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-const-variable','-Wno-comment','-fsanitize=address,undefined','-I',str(sdk/'ui/FreeInkUI/include'),'-I',str(sdk/'assets/Icons/include'),str(p/'test.cpp'),str(sdk/'ui/FreeInkUI/src/FreeInkUI.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Actual CC/FUI layout passed: 32 orientation/reader/light/audio combinations, sliders present, 88px targets, 8px gaps, no overflow')
