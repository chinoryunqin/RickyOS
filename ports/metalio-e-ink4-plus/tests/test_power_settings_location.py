"""Execute the production timing catalog entries and product-category filter."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
UP=ROOT/'firmware/rickyos-plus-1.2/upstream'
catalog=(UP/'src/SettingsList.h').read_text()
settings=(UP/'src/activities/settings/SettingsActivity.cpp').read_text()
wallpaper=(UP/'src/activities/apps/standby/RickyStandbySettingsActivity.cpp').read_text()
a=catalog.index('inline constexpr StrId AUTO_POWER_OFF_LABELS[]')
labels=catalog[a:catalog.index('// Shared settings list',a)]
a=catalog.index('        SettingInfo::Value(\n            StrId::STR_RICKY_AUTO_STANDBY')
entries=catalog[a:catalog.index('        // --- System ---',a)]
a=settings.index('  const auto elsewhere = ')
b=settings.index('  for (auto* list',a)
filter_code=settings[a:b]
ids=sorted(set(re.findall(r'StrId::(STR_\w+)',labels+entries)))
fields=set(re.findall(r'&CrossPointSettings::(\w+)',filter_code+entries))
assert 'add(AutoStandby)' not in wallpaper and 'add(AutoPowerOff)' not in wallpaper
assert 'case AutoStandby:' not in wallpaper and 'case AutoPowerOff:' not in wallpaper
assert 'if (setting.valuePtr == &CrossPointSettings::sleepTimeoutMinutes)' in settings
program=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
'''+'enum class StrId {'+','.join(ids)+'};\n'+r'''
enum class SettingType {VALUE,ENUM};
enum class SettingAction {None,SdFirmwareUpdate};
struct CrossPointSettings {
 static constexpr int MIN_SLEEP_TIMEOUT_MINUTES=1,MAX_SLEEP_TIMEOUT_MINUTES=31,RICKY_AUTO_OFF_COUNT=6;
'''+''.join(' uint8_t '+name+'=0;\n' for name in sorted(fields))+r'''
};
struct SettingInfo {
 struct ValueRange {int min,max,step;};
 StrId nameId,category;SettingType type;
 uint8_t CrossPointSettings::* valuePtr;
 const char* key;ValueRange valueRange{};std::span<const StrId> labels;
 SettingAction action=SettingAction::None;
 static SettingInfo Value(StrId n,uint8_t CrossPointSettings::*p,ValueRange r,const char*k,StrId c){return {n,c,SettingType::VALUE,p,k,r,{}};}
 static SettingInfo StaticEnum(StrId n,uint8_t CrossPointSettings::*p,std::span<const StrId>v,const char*k,StrId c){return {n,c,SettingType::ENUM,p,k,{},v};}
};
'''+labels+r'''
int main(){
 const SettingInfo entries[]={
'''+entries+r'''
 };
'''+filter_code+r'''
 assert(std::size(entries)==2);
 CrossPointSettings saved;saved.sleepTimeoutMinutes=7;saved.rickyAutoOffIndex=4;
 for(const auto& item:entries){assert(item.category==StrId::STR_CAT_DISPLAY);assert(!elsewhere(item));}
 const auto& standby=entries[0];const auto& off=entries[1];
 assert(standby.valuePtr==&CrossPointSettings::sleepTimeoutMinutes);
 assert(off.valuePtr==&CrossPointSettings::rickyAutoOffIndex);
 assert(std::strcmp(standby.key,"sleepTimeoutMinutes")==0&&std::strcmp(off.key,"rickyAutoOffIndex")==0);
 assert(standby.valueRange.min==1&&standby.valueRange.max==31&&standby.valueRange.step==1);
 assert(off.labels.size()==6&&off.labels[0]==StrId::STR_RICKY_NEVER&&off.labels[5]==StrId::STR_RICKY_HOURS_24);
 assert(saved.*standby.valuePtr==7&&saved.*off.valuePtr==4);
}
'''
with tempfile.TemporaryDirectory(prefix='power-settings-location-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(program)
 subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Both timing entries survive product filtering in Display & Standby; stored fields, values, ranges and hour labels preserved; wallpaper timing controls removed.')
