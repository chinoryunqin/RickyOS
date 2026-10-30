"""Route real SDK hit regions from the production statistics row provider."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UP = ROOT / 'firmware/rickyos-plus-1.2/upstream'
s = (UP / 'src/activities/settings/ReadingStatsSettingsActivity.cpp').read_text()

def method(text, signature):
    a = text.index(signature)
    b = text.index('{', a) + 1
    depth = 1
    while depth:
        depth += (text[b] == '{') - (text[b] == '}')
        b += 1
    return text[a:b]

constants = s[s.index('enum class MenuItem'):s.index('}  // namespace')]
a = s.index('  props.rowProvider = ')
provider = s[a:s.index('  };', a) + 4]
ids = sorted(set(re.findall(r'StrId::(STR_\w+)', constants + method(s, 'void ReadingStatsSettingsActivity::handleSelection()'))))
control = (ROOT / 'tests/test_plus_control_center_layout.py').read_text()
target = control[control.index('struct Target : fui::DrawTarget {'):control.index('fui::ThemeTokens tokens()')]
checkbox = method((UP / 'src/components/themes/BaseTheme.cpp').read_text(), 'void BaseTheme::setCheckboxRow(')
program = r'''
#include <FreeInkUI.h>
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
namespace fui=freeink::ui;
''' + 'enum class StrId {' + ','.join(ids) + '};\n' + r'''
struct CrossPointSettings {
 static constexpr int DAILY_GOAL_TARGET_COUNT=4,DAILY_GOAL_30_MIN=1;
 uint8_t dailyGoalTarget=1,achievementsEnabled=1,achievementPopups=0;
 int saves=0;void saveToFile(){++saves;}
} SETTINGS;
struct {const char* get(StrId)const{return "setting";}} I18N;
struct BaseTheme {static void setCheckboxRow(fui::ListItem&,bool);};
''' + checkbox + r'''
BaseTheme GUI;
struct Popup {
 int opens=0;std::function<void(int)> callback;
 template<class L,class F>void show(StrId,const L&,int,int,F f){++opens;callback=f;}
};
struct ReadingStatsSettingsActivity {
 Popup optionPopup;
 struct {void clearTapFlash(){}} app;
 int selectedIndex=0,updates=0;
 void requestUpdate(){++updates;}
 void handleSelection();void activateIndex(int index);
};
''' + constants + method(s, 'void ReadingStatsSettingsActivity::handleSelection()') + method(s, 'void ReadingStatsSettingsActivity::activateIndex(') + target + r'''
int main(){
 for(auto size:{std::pair{684,1216},std::pair{1216,684}}){
  Target target;fui::DeviceContext device;device.width=size.first;device.height=size.second;device.hasTouch=true;
  fui::InputSnapshot initial;fui::InteractionBuffer<8> hits;fui::Frame<8> frame(target,device,initial,hits);
  fui::ListProps props;props.count=MENU_ITEMS;props.action=42;props.inputMask=fui::InputTouch;
  props.rowHeight=96;props.rowGap=8;
''' + provider + r'''
  fui::list(frame,{0,100,static_cast<int16_t>(size.first),400},props);
  assert(hits.count()==3&&!hits.overflowed());
  for(int row=0;row<3;++row){
   auto hit=hits.data()[row];assert(hit.value==row);
   for(int x:{1,size.first/2,size.first-2}){
    SETTINGS={};ReadingStatsSettingsActivity activity;
    fui::InputSnapshot tap;tap.touchReleased=true;tap.touchX=x;tap.touchY=hit.rect.y+hit.rect.height/2;
    auto event=hits.route(tap);assert(event.action==42&&event.value==row);
    activity.activateIndex(event.value);
    assert(activity.selectedIndex==row);
    if(row==0){
      assert(activity.optionPopup.opens==1&&SETTINGS.saves==0);
      activity.optionPopup.callback(3);assert(SETTINGS.dailyGoalTarget==3&&SETTINGS.saves==1);
    }else{
      assert(activity.optionPopup.opens==0&&SETTINGS.dailyGoalTarget==1&&SETTINGS.saves==1);
      assert(SETTINGS.achievementsEnabled==(row==1?0:1));
      assert(SETTINGS.achievementPopups==(row==2?1:0));
    }
   }
  }
  fui::InputSnapshot blank;blank.touchReleased=true;blank.touchX=20;blank.touchY=450;
  assert(!hits.route(blank));
 }
}
'''
with tempfile.TemporaryDirectory(prefix='stats-settings-touch-') as directory:
    p=Path(directory);(p/'test.cpp').write_text(program)
    subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-const-variable','-fsanitize=address,undefined',
                    '-I'+str(UP/'freeink-sdk/libs/ui/FreeInkUI/include'),str(p/'test.cpp'),str(UP/'freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Statistics settings: real row provider + SDK hit routing + production selection handlers passed for all rows and both orientations.')
