"""Native regressions for persisted shelf folders and numeric reader spacing."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts/tests"))
from test_reading_ui_regressions import method

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT / "firmware/rickyos-plus-1.2/upstream"
JSON = PORT / '.local/dependencies/ArduinoJson/src'


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


class ReaderPreferencesTest(unittest.TestCase):
    def test_folder_json_roundtrip_and_validation(self):
        source = (ROOT / 'src/CrossPointState.cpp').read_text()
        production = source[source.index('namespace {'):source.index('bool CrossPointState::loadFromFile()')]
        production = re.sub(r'^constexpr .*STATE_FILE_.*\n', '', production, flags=re.M)
        compile_run(r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include "CrossPointState.h"
#define LOG_ERR(...) ((void)0)
''' + production + r'''
int main() {
  auto& state = CrossPointState::getInstance();
  JsonDocument legacy;
  state.fromJson(legacy.as<JsonVariantConst>());
  assert(std::string(state.libraryFolderPath).empty());
  for (const std::string& path : {std::string("/books/小说/科幻"), std::string("/books"),
                                 "/" + std::string(254, 'a'), std::string("")}) {
    const int before = state.saveCount;
    assert(state.rememberLibraryFolder(path));
    assert(state.saveCount == before + 1);
    const std::string disk = state.saved;
    // Simulate a new session, not merely a retained Activity member.
    state.libraryFolderPath[0] = '\0';
    JsonDocument restored;
    assert(!deserializeJson(restored, disk));
    state.fromJson(restored.as<JsonVariantConst>());
    assert(path == state.libraryFolderPath);
    assert(state.rememberLibraryFolder(path));
    assert(state.saveCount == before + 1); // no redundant card writes
  }
  assert(state.rememberLibraryFolder("/books/keep"));
  for (const std::string& invalid : {std::string("relative"), "/" + std::string(255, 'a'),
                                    std::string("/books\0other", 12)}) {
    assert(!state.rememberLibraryFolder(invalid));
    assert(std::string(state.libraryFolderPath) == "/books/keep");
  }
  for (const std::string& invalid : {std::string("relative"), "/" + std::string(255, 'a')}) {
    JsonDocument bad; bad["libraryFolderPath"] = invalid;
    state.fromJson(bad.as<JsonVariantConst>());
    assert(state.libraryFolderPath[0] == '\0');
  }
  state.saveOk = false;
  assert(!state.rememberLibraryFolder("/books/temporary"));
  assert(std::string(state.libraryFolderPath) == "/books/temporary");
}
''', {'PersistableStore.h': r'''
#pragma once
#include <ArduinoJson.h>
#include <mutex>
#include <string>
template<class T> class PersistableStore {
protected:
  std::mutex storeMutex;
public:
  int saveCount=0;
  bool saveOk=true;
  std::string saved;
  static T& getInstance() { static T object; return object; }
  bool saveToFile() {
    ++saveCount;
    if(!saveOk) return false;
    JsonDocument doc;
    static_cast<T*>(this)->toJson(doc);
    saved.clear();serializeJson(doc,saved);return true;
  }
};
'''})

    def test_numeric_spacing_legacy_persistence_and_cache_spec(self):
        source = (ROOT / 'src/CrossPointSettings.cpp').read_text()
        header = (ROOT / 'src/CrossPointSettings.h').read_text()
        load_start = source.index('  const uint16_t spacingPercent = doc["lineSpacingPercent"]')
        load_end = source.index('  standbyCoverLayout', load_start)
        load = source[load_start:load_end]
        definitions = '\n'.join(re.findall(r'  static constexpr uint16_t LINE_SPACING_PERCENT_\w+ = \d+;', header))
        functions = '\n'.join(method(source, signature) for signature in (
            'uint16_t CrossPointSettings::getReaderLineSpacingPercent() const',
            'float CrossPointSettings::getReaderLineCompression() const',
            'ReaderRenderSpec CrossPointSettings::readerRenderSpec('))
        compile_run(r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include "Epub/ReaderRenderSpec.h"
struct CrossPointSettings {
  enum { TIGHT=0, NORMAL=1, WIDE=2, EXTRA_WIDE=3, NOTOSERIF=0, NOTOSANS=1 };
''' + definitions + r'''
  uint8_t lineSpacing=NORMAL, fontFamily=NOTOSERIF;
  uint16_t lineSpacingPercent=0;
  char sdFontFamilyName[32]={};
  uint8_t extraParagraphSpacing=0, firstLineIndent=0, paragraphIndentSpaces=3;
  uint8_t wordSpacing=100, paragraphAlignment=0, hyphenationEnabled=0, embeddedStyle=1;
  uint8_t imageRendering=0, focusReadingEnabled=0;
  int getReaderFontId()const{return 1;} int8_t getCharacterSpacing()const{return 0;}
  float getReaderLineCompression()const;
  uint16_t getReaderLineSpacingPercent()const;
  ReaderRenderSpec readerRenderSpec(uint16_t,uint16_t)const;
  void load(JsonVariantConst doc){
''' + load + r'''
  }
  void save(JsonDocument& doc)const {
''' + re.search(r'  doc\["lineSpacingPercent"\] = lineSpacingPercent;', source)[0] + r'''
  }
};
''' + functions + r'''
int main() {
  CrossPointSettings s;
  const float expected[3][4]={{0.95f,1.0f,1.1f,1.2f},{0.90f,0.95f,1.0f,1.05f},{0.95f,1.0f,1.3f,1.6f}};
  for(int profile=0;profile<3;++profile) {
    s.fontFamily=profile==1?CrossPointSettings::NOTOSANS:CrossPointSettings::NOTOSERIF;
    s.sdFontFamilyName[0]=profile==2?'X':'\0';
    JsonDocument legacy;s.load(legacy.as<JsonVariantConst>());
    for(int index=0;index<4;++index) {
      s.lineSpacing=index;
      assert(std::abs(s.getReaderLineCompression()-expected[profile][index])<0.00001f);
      assert(s.getReaderLineSpacingPercent()==int(expected[profile][index]*100+0.5f));
    }
    for(int percent=80;percent<=400;++percent) {
      JsonDocument doc;doc["lineSpacingPercent"]=percent;s.load(doc.as<JsonVariantConst>());
      assert(std::abs(s.getReaderLineCompression()-percent/100.0f)<0.00001f);
      assert(s.getReaderLineSpacingPercent()==percent);
      JsonDocument saved;s.save(saved);assert(saved["lineSpacingPercent"]==percent);
      auto first=s.readerRenderSpec(600,900);
      s.lineSpacingPercent=percent==400?399:percent+1;
      assert(!(first==s.readerRenderSpec(600,900))); // stale section/page cache rejected
    }
  }
  for(int bad : {-100,-1,0,79,401,65535,65536,100000}) {
    JsonDocument doc;doc["lineSpacingPercent"]=bad;s.load(doc.as<JsonVariantConst>());
    assert(s.lineSpacingPercent==0);
  }
  for(const char* bad : {"99", "invalid"}) {
    JsonDocument doc;doc["lineSpacingPercent"]=bad;s.load(doc.as<JsonVariantConst>());
    assert(s.lineSpacingPercent==0);
  }
}
''')


    def test_reader_numeric_dialog_confirm_cancel_and_resume(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        popup = method(source, 'void EpubReaderActivity::showTextRowPopup(')
        start = popup.index('    case kTextRowSpacing:')
        spacing_case = popup[start:popup.index('    case kTextRowAlignment:', start)]
        compile_run(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <variant>
enum class StrId {STR_LINE_SPACING, STR_LINE_SPACING_VALUE};
// Xtensa's int32_t is long, unlike this host's int32_t. Keep that distinction.
struct IntervalResult {long value;};
struct ActivityResult {bool isCancelled=false;std::variant<IntervalResult,int> data;};
struct IntervalSelectionActivity {};
struct RenderLock {~RenderLock(){}};
struct CrossPointSettings {
  static constexpr int LINE_SPACING_PERCENT_MIN=80, LINE_SPACING_PERCENT_MAX=400;
  uint16_t lineSpacingPercent=0;
  int getReaderLineSpacingPercent()const{return lineSpacingPercent?lineSpacingPercent:95;}
} SETTINGS;
struct Toolbar {int starts=0;void begin(){++starts;}};
struct Host {
  static constexpr int kTextRowSpacing=3;
  Toolbar toolbar;Toolbar* toolbarUi=&toolbar;
  int settled=0, discarded=0, applied=0, updates=0;
  std::function<void(const ActivityResult&)> callback;
  void settleOverlayRefresh(){++settled;}
  void discardOverlayPage(){++discarded;}
  void applyTextSettingLive(){++applied;}
  void requestUpdate(){++updates;}
  template<class T,class F> bool startActivityForResultWith(F fn,const char*,StrId,int initial,
      int minimum,int maximum,int small,int large,StrId,bool reader){
    assert(initial==95);assert(minimum==80);assert(maximum==400);
    assert(small==1);assert(large==5);assert(reader);callback=fn;return true;
  }
  void show(){switch(kTextRowSpacing){
''' + spacing_case + r'''
  }}
};
int main(){
  Host host;host.show();assert(host.settled==1);assert(host.discarded==1);
  ActivityResult result;result.isCancelled=true;result.data=IntervalResult{123};host.callback(result);
  assert(SETTINGS.lineSpacingPercent==0);assert(host.applied==0);assert(host.updates==1);
  assert(host.toolbar.starts==1);
  result.isCancelled=false;result.data=IntervalResult{123};host.callback(result);
  assert(SETTINGS.lineSpacingPercent==123);assert(host.applied==1);
  result.data=IntervalResult{256};host.callback(result);assert(SETTINGS.lineSpacingPercent==256);
  result.data=IntervalResult{399};host.callback(result);assert(SETTINGS.lineSpacingPercent==399);
  result.data=IntervalResult{-10};host.callback(result);assert(SETTINGS.lineSpacingPercent==80);
  result.data=IntervalResult{999};host.callback(result);assert(SETTINGS.lineSpacingPercent==400);
  result.data=42;host.callback(result);assert(SETTINGS.lineSpacingPercent==400);
}
''')


if __name__ == '__main__':
    unittest.main()
