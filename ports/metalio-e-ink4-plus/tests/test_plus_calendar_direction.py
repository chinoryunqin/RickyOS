"""Run actual calendar orientation and JSON fragments, preserving caller orientation."""
from pathlib import Path
import subprocess
import tempfile

up = Path('firmware/rickyos-plus-1.2/upstream')
activity = (up / 'src/activities/apps/standby/StandbyActivity.cpp').read_text()
start = activity.index('  savedOrientation_ = renderer.getOrientation();')
enter = activity[start:activity.index('  LOG_DBG(', start)]
exit_start = activity.index('void StandbyActivity::onExit()')
restore_start = activity.index('  renderer.setOrientation(savedOrientation_);', exit_start)
restore = activity[restore_start:activity.index('\n', restore_start)]
settings = (up / 'src/CrossPointSettings.cpp').read_text()
save = next(line for line in settings.splitlines() if 'doc["standbyCalendarReversed"] = ' in line)
load = next(line for line in settings.splitlines() if 'standbyCalendarReversed = clamp(' in line)
program = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <string>
struct CrossPointSettings{
 enum {RICKY_STANDBY_PICTURE,RICKY_STANDBY_CALENDAR,RICKY_STANDBY_COVER,RICKY_STANDBY_KEEP_PAGE,RICKY_STANDBY_TRANSPARENT};
 uint8_t standbyCalendarReversed=0,rickyStandbyFace=0,orientation=2;
 static uint8_t clamp(uint8_t v,uint8_t max,uint8_t def){return v<max?v:def;}
 void save(JsonVariant doc){SAVE}
 void load(JsonVariantConst doc){LOAD}
} SETTINGS;
struct GfxRenderer{
 enum class Orientation{Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};
 Orientation value=Orientation::Portrait;
 Orientation getOrientation()const{return value;}
 void setOrientation(Orientation o){value=o;}
};
struct StandbyActivity{
 GfxRenderer renderer;GfxRenderer::Orientation savedOrientation_;
 void enter(){ENTER}
 void exit(){RESTORE}
};
int main(){
 JsonDocument doc;CrossPointSettings reboot;
 for(int flip=0;flip<2;++flip){
  SETTINGS.standbyCalendarReversed=flip;SETTINGS.save(doc.to<JsonObject>());
  std::string encoded;serializeJson(doc,encoded);doc.clear();assert(!deserializeJson(doc,encoded));
  reboot.load(doc.as<JsonVariantConst>());assert(reboot.standbyCalendarReversed==flip);
  for(int original=0;original<4;++original)for(int face=0;face<5;++face){
   SETTINGS.rickyStandbyFace=face;StandbyActivity s;
   s.renderer.value=static_cast<GfxRenderer::Orientation>(original);s.enter();
   const int expected=face==CrossPointSettings::RICKY_STANDBY_CALENDAR?(flip?3:1):original;
   assert(static_cast<int>(s.renderer.value)==expected);
   s.exit();assert(static_cast<int>(s.renderer.value)==original);
   assert(SETTINGS.orientation==2); // No reading preference is overwritten.
  }
 }
 for(const char* bad:{"{}",R"({"standbyCalendarReversed":255})",R"({"standbyCalendarReversed":-1})",
                      R"({"standbyCalendarReversed":"invalid"})"}){
  deserializeJson(doc,bad);reboot.standbyCalendarReversed=1;
  reboot.load(doc.as<JsonVariantConst>());assert(reboot.standbyCalendarReversed==0);
 }
}
'''
for marker, body in [('SAVE', save), ('LOAD', load), ('ENTER', enter), ('RESTORE', restore)]:
    program = program.replace(marker, body)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(program)
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I.local/dependencies/ArduinoJson/src', str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('Calendar direction: production CW/CCW selection, all caller rotations restored, other faces unchanged, '
      'reading setting retained, JSON restart round trip/default/invalid input passed.')
