"""Compile actual restored Plus paragraph-spacing migration and slider callbacks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
JSON = next(path for path in sorted((ROOT / '.pio/libdeps').glob('*/ArduinoJson/src'))
            if (path / 'ArduinoJson.h').is_file())

def method(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def compile_run(program, headers=None):
    with tempfile.TemporaryDirectory(prefix='reader-preferences-') as directory:
        work = Path(directory)
        for name, content in (headers or {}).items():
            (work / name).write_text(content)
        (work / 'test.cpp').write_text(program)
        subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(work), '-I' + str(JSON), '-I' + str(ROOT / 'src'),
                        '-I' + str(ROOT / 'lib/Epub'), str(work / 'test.cpp'),
                        '-o', str(work / 'test')], check=True)
        subprocess.run([str(work / 'test')], check=True)


class ParagraphSpacingTest(unittest.TestCase):
    def test_web_percentage_roundtrip_without_byte_truncation(self):
        source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
        getter = method(method(source, 'void CrossPointWebServer::handleGetSettings('),
                        '      case SettingType::VALUE:')
        setter = method(method(source, 'void CrossPointWebServer::handlePostSettings('),
                        '      case SettingType::VALUE:')
        compile_run(r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
struct CrossPointSettings { uint16_t paragraphSpacingPercent=100; } SETTINGS;
struct SettingInfo {
  uint8_t CrossPointSettings::*valuePtr=nullptr;
  uint16_t CrossPointSettings::*wideValuePtr=&CrossPointSettings::paragraphSpacingPercent;
  int8_t CrossPointSettings::*signedValuePtr=nullptr;
  const char* key="paragraphSpacingScalePercent";
  struct { int min=0,max=400,step=1; } valueRange;
} s;
enum class SettingType { VALUE };
int main() {
  for(int percent:{0,100,200,255,256,399,400,-1,401,65536}) {
    SETTINGS.paragraphSpacingPercent=100;
    JsonDocument doc;doc[s.key]=percent;int applied=0;
    switch(SettingType::VALUE) {
''' + setter + r'''
    }
    const bool valid=percent>=0&&percent<=400;
    assert(applied==(valid?1:0));
    assert(SETTINGS.paragraphSpacingPercent==(valid?percent:100));
    doc.clear();
    switch(SettingType::VALUE) {
''' + getter + r'''
    }
    assert(doc["value"]==(valid?percent:100));
    assert(doc["max"]==400);
  }
}
''')

    def test_paragraph_spacing_migration_validation_and_persistence(self):
        source = (ROOT / 'src/CrossPointSettings.cpp').read_text()
        start = source.index('  const auto savedParagraphSpacing =')
        load = source[start:source.index('  copyToField(uiFontFamily,', start)]
        serializer = method(method(source, 'void CrossPointSettings::toJson('),
                            '  for (const auto& info : getBaseSettingsList())')
        binary = next(line for line in source.splitlines() if 'paragraphSpacing::fromLegacy(values[1])' in line)
        compile_run(r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "Epub/ParagraphSpacing.h"
struct CrossPointSettings;
struct SettingInfo {
  const char* key="paragraphSpacingScalePercent";
  uint16_t CrossPointSettings::*wideValuePtr;
  uint8_t CrossPointSettings::*valuePtr=nullptr;
  int8_t CrossPointSettings::*signedValuePtr=nullptr;
  size_t stringOffset=0;
  bool obfuscated=false;
};
namespace obfuscation {const char* obfuscateToBase64(const char* text){return text;}}
constexpr size_t OBF_KEY_BUF=64;
bool isSettingAvailableForPersistence(const SettingInfo&){return true;}
std::vector<SettingInfo> getBaseSettingsList();
struct CrossPointSettings {
  uint16_t paragraphSpacingPercent=0;
  bool needsResave=false;
  void load(JsonVariantConst doc) {
    needsResave=false;
''' + load + r'''
  }
  void loadBinary(int fileSettingsCount,const uint8_t* values) {
''' + binary + r'''
  }
  void toJson(JsonDocument& doc) const {
    const CrossPointSettings& s=*this;
''' + serializer + r'''
  }
};
std::vector<SettingInfo> getBaseSettingsList(){return {{"paragraphSpacingScalePercent",&CrossPointSettings::paragraphSpacingPercent}};}
int main() {
  CrossPointSettings s;
  const int expected[]={100,150,175,200,200,200};
  for(int level=0;level<6;++level) {
    JsonDocument old;old["extraParagraphSpacing"]=level;s.load(old.as<JsonVariantConst>());
    assert(s.paragraphSpacingPercent==expected[level]);assert(s.needsResave);
    const uint8_t bytes[]={0,static_cast<uint8_t>(level)};
    s.paragraphSpacingPercent=77;s.loadBinary(2,bytes);assert(s.paragraphSpacingPercent==expected[level]);
    s.paragraphSpacingPercent=77;s.loadBinary(1,bytes);assert(s.paragraphSpacingPercent==77);
  }
  for(bool enabled:{false,true}) {
    JsonDocument old;old["extraParagraphSpacing"]=enabled;s.load(old.as<JsonVariantConst>());
    assert(s.paragraphSpacingPercent==(enabled?150:100));
  }
  { JsonDocument missing;s.load(missing.as<JsonVariantConst>());assert(s.paragraphSpacingPercent==100); }
  for(int oldPercent:{0,1,50,100,150,200}) {
    JsonDocument old;old["paragraphSpacingPercent"]=oldPercent;s.load(old.as<JsonVariantConst>());
    assert(s.paragraphSpacingPercent==paragraphSpacing::fromAdditional(oldPercent));assert(s.needsResave);
  }
  for(int percent=0;percent<=400;++percent) {
    JsonDocument doc;doc["paragraphSpacingScalePercent"]=percent;doc["extraParagraphSpacing"]=5;
    s.load(doc.as<JsonVariantConst>());assert(s.paragraphSpacingPercent==percent);assert(!s.needsResave);
    JsonDocument saved;s.toJson(saved);
    assert(saved["paragraphSpacingScalePercent"]==percent);assert(saved["extraParagraphSpacing"].isNull());
    std::string disk;serializeJson(saved,disk);
    JsonDocument reread;assert(!deserializeJson(reread,disk));
    s.paragraphSpacingPercent=255;s.load(reread.as<JsonVariantConst>());
    assert(s.paragraphSpacingPercent==percent);
    // Arbitrary font and line-spacing changes use the same integer geometry.
    assert(paragraphSpacing::gapPixels(40,percent)==(percent>100?40*(percent-100)/100:0));
    assert(paragraphSpacing::gapPixels(67,percent)==(percent>100?67*(percent-100)/100:0));
  }
  for(int bad:{-1,401,65535,65536}) {
    JsonDocument doc;doc["paragraphSpacingScalePercent"]=bad;s.load(doc.as<JsonVariantConst>());
    assert(s.paragraphSpacingPercent==100);assert(s.needsResave);
  }
  for(const char* bad:{"75","invalid"}) {
    JsonDocument doc;doc["paragraphSpacingScalePercent"]=bad;s.load(doc.as<JsonVariantConst>());
    assert(s.paragraphSpacingPercent==100);assert(s.needsResave);
  }
}
''')

    def test_paragraph_slider_confirmation_cancellation_and_limits(self):
        source = (ROOT / 'src/activities/settings/TextSettingsActivity.cpp').read_text()
        body = method(source, 'void TextSettingsActivity::confirmLayoutRow(')
        start = body.index('    case LayoutRow::ParaSpacing:')
        spacing_case = body[start:body.index('    case LayoutRow::FirstLineIndent:', start)]
        compile_run(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <variant>
#include "Epub/ParagraphSpacing.h"
enum class StrId{STR_PARAGRAPH_SPACING,STR_LINE_SPACING_VALUE};
enum class LayoutRow{ParaSpacing};
struct IntervalResult{long value;};
struct ActivityResult{bool isCancelled=false;std::variant<IntervalResult,int> data;};
struct IntervalSelectionActivity{};
struct {uint16_t paragraphSpacingPercent=75;int writes=0;void saveToFile(){++writes;}} SETTINGS;
struct Host {
  int updates=0;std::function<void(const ActivityResult&)> callback;
  void requestUpdate(){++updates;}
  template<class T,class F> void startActivityForResultWith(F fn,const char*,StrId title,int initial,
      int minimum,int maximum,int small,int large,StrId format) {
    assert(title==StrId::STR_PARAGRAPH_SPACING);assert(format==StrId::STR_LINE_SPACING_VALUE);
    assert(initial==75);assert(minimum==0);assert(maximum==400);assert(small==1);assert(large==5);callback=fn;
  }
  void open(){switch(LayoutRow::ParaSpacing){
''' + spacing_case + r'''
  }}
};
int main() {
  Host host;host.open();
  ActivityResult result;result.isCancelled=true;result.data=IntervalResult{123};host.callback(result);
  assert(SETTINGS.paragraphSpacingPercent==75);assert(SETTINGS.writes==0);assert(host.updates==1);
  result.isCancelled=false;result.data=42;host.callback(result);assert(SETTINGS.writes==0);
  for(int value:{0,1,33,99,100,150,199,200,255,256,399,400,-5,999}) {
    result.data=IntervalResult{value};host.callback(result);
    assert(SETTINGS.paragraphSpacingPercent==std::clamp(value,0,400));
  }
  assert(SETTINGS.writes==14);
}
''')


if __name__ == '__main__':
    unittest.main()
