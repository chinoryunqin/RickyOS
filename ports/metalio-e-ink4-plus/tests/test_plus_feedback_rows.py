"""Run the actual Plus settings row provider and check its owning list capacity."""
from pathlib import Path
import re
import subprocess
import tempfile

up = Path('firmware/rickyos-plus-1.2/upstream')
text = (up / 'src/activities/settings/RickyOptionListActivity.cpp').read_text()
start = text.index('const RickyOptionList& keys() {')
cursor = text.index('{', start) + 1
depth = 1
while depth:
    depth += (text[cursor] == '{') - (text[cursor] == '}')
    cursor += 1
keys = text[start:cursor]
header = (up / 'src/activities/settings/HomeButtonSettingsActivity.h').read_text()
capacity = int(re.search(r'GESTURE_COUNT\s*=\s*(\d+)', header).group(1))
program = r'''
#include <cassert>
#include <cstdint>
#include <iterator>
#define FREEINK_DEVICE_METALIO_PLUS 1
enum class StrId{STR_RICKY_KEYS,STR_PLUS_TOUCH_FEEDBACK,STR_PLUS_SIDE_KEY_FEEDBACK};
struct Settings {uint8_t plusTouchFeedback=1,plusSideKeyFeedback=0;} SETTINGS;
struct RickyOptionRow{StrId label;uint8_t* field;};
struct RickyOptionList{StrId title;const RickyOptionRow* rows;int count;};
''' + keys + r'''
int main(){
 const auto& list=keys();assert(list.count==2);
 assert(list.rows[0].field==&SETTINGS.plusTouchFeedback);
 assert(list.rows[1].field==&SETTINGS.plusSideKeyFeedback);
 assert(list.rows[0].label==StrId::STR_PLUS_TOUCH_FEEDBACK);
 assert(list.rows[1].label==StrId::STR_PLUS_SIDE_KEY_FEEDBACK);
 assert(6+list.count+1==CAPACITY);
 for(int i=0;i<list.count;++i){
  const auto touch=SETTINGS.plusTouchFeedback,side=SETTINGS.plusSideKeyFeedback;
  *list.rows[i].field=!*list.rows[i].field;
  assert(SETTINGS.plusTouchFeedback==(i==0?!touch:touch));
  assert(SETTINGS.plusSideKeyFeedback==(i==1?!side:side));
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(program.replace('CAPACITY', str(capacity)))
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('Production Plus feedback rows: independent fields/labels, correct parent list capacity and toggle isolation.')
