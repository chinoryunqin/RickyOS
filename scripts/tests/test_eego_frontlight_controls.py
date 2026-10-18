"""Exercise actual control-center light callbacks and layout against a fake HAL."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[2] / 'src/activities/util/FrontlightPanelActivity.cpp').read_text()
signatures = [
    'uint8_t percentFromPermille(',
    'void FrontlightPanelActivity::onBrightnessEvent(',
    'void FrontlightPanelActivity::onWarmthEvent(',
    'void FrontlightPanelActivity::onTileEvent(',
    'void FrontlightPanelActivity::adjustBrightness(',
    'void FrontlightPanelActivity::adjustWarmth(',
    'void FrontlightPanelActivity::toggleLight(',
    'void FrontlightPanelActivity::persistLightSettings(',
]
functions = []
for signature in signatures:
    start = source.index(signature)
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    functions.append(source[start:cursor])

program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
constexpr uint8_t MIN_BRIGHTNESS = 1;
namespace fui { struct ActionEvent { int16_t dragPermille=-1; int value=0; }; }
struct Light {
 bool lit=false, available=true, dual=true;
 uint8_t brightness=60, warmth=50;
 int writes=0;
 void setBrightness(uint8_t v) { brightness=v; ++writes; }
 void setWarmth(uint8_t v) { warmth=v; ++writes; }
 void setOn(bool v) { lit=v; ++writes; }
 bool present() const { return available; }
 bool hasColorTemperature() const { return dual; }
} Frontlight;
struct Settings {
 uint8_t frontlightBrightness=60, frontlightWarmth=50, frontlightOn=1;
 int saves=0;
 void saveToFile() { ++saves; }
} SETTINGS;
class FrontlightPanelActivity {
public:
 uint8_t brightness=60, warmth=50;
 bool lightOn=false, lightOnChanged=false, overReader=false;
 int updates=0;
 struct App {void clearTapFlash(){}} app;void close() {}
 void requestUpdate() { ++updates; }
 static void onBrightnessEvent(const fui::ActionEvent&,void*);
 static void onWarmthEvent(const fui::ActionEvent&,void*);
 static void onTileEvent(const fui::ActionEvent&,void*);
 void runTile(int) {}
 void adjustBrightness(int); void adjustWarmth(int); void toggleLight();
 void persistLightSettings();
};
FUNCTIONS
int main() {
 FrontlightPanelActivity p;
 // Merely visiting after wake-with-light-off must preserve saved On preference.
 p.persistLightSettings(); assert(SETTINGS.saves==0 && SETTINGS.frontlightOn==1);
 p.onBrightnessEvent({600},&p); assert(p.lightOn && Frontlight.lit);
 p.onBrightnessEvent({0},&p); assert(p.brightness==1 && Frontlight.brightness==1);
 p.onBrightnessEvent({1000},&p); assert(p.brightness==100);
 int writes=Frontlight.writes; p.onBrightnessEvent({1000},&p); assert(Frontlight.writes==writes);
 p.onBrightnessEvent({-1},&p); assert(Frontlight.writes==writes);
 p.toggleLight(); assert(!p.lightOn && p.brightness==100);
 p.onWarmthEvent({1000},&p); assert(p.warmth==100 && !p.lightOn && !Frontlight.lit);
 p.onWarmthEvent({0},&p); assert(p.warmth==0);
 p.adjustWarmth(-1); assert(p.warmth==0);
 p.adjustWarmth(101); assert(p.warmth==100);
 p.toggleLight(); assert(p.lightOn && p.brightness==100 && p.warmth==100);
 p.persistLightSettings(); assert(SETTINGS.saves==1 && SETTINGS.frontlightBrightness==100);
 p.persistLightSettings(); assert(SETTINGS.saves==1);
 p.adjustBrightness(-200); assert(p.brightness==1);
 p.adjustBrightness(200); assert(p.brightness==100);
 p.toggleLight(); p.persistLightSettings(); assert(SETTINGS.frontlightOn==0);
 Frontlight.available=false; SETTINGS.frontlightBrightness=17; SETTINGS.frontlightWarmth=83;
 int saves=SETTINGS.saves; p.persistLightSettings();
 assert(SETTINGS.saves==saves && SETTINGS.frontlightBrightness==17 && SETTINGS.frontlightWarmth==83);

}
'''.replace('FUNCTIONS', '\n'.join(functions))
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root/'panel.cpp').write_text(program)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',str(root/'panel.cpp'),'-o',str(root/'panel')],check=True)
    subprocess.run([str(root/'panel')],check=True)
print('Actual light callbacks: clamps, no duplicate writes, off-state memory, save-on-change and absent-light preference preservation passed')
