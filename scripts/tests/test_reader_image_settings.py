"""Execute production tone and per-book/default persistence with injected I/O failures."""
from pathlib import Path
from test_reading_ui_regressions import method
import subprocess
import os
import tempfile

ROOT = Path(os.environ.get('RICKYOS_READER_SOURCE_ROOT', Path(__file__).resolve().parents[2]))
JSON = (Path(os.environ['RICKYOS_ARDUINOJSON_INCLUDE']) if 'RICKYOS_ARDUINOJSON_INCLUDE' in os.environ
        else next(ROOT.glob('.pio/libdeps/*/ArduinoJson/src')))


def main():
    strings = (ROOT / 'lib/I18n/translations/chinese.yaml').read_text().split('STR_READER_IMAGE_SETTINGS:', 1)[1]
    if (ROOT / 'src/resources/SystemUiFont_sc_regular.h').is_file():
        from test_settings_translations import outline_codepoints
        coverage = outline_codepoints(ROOT / 'src/resources/SystemUiFont_sc_regular.h')
        missing = {c for c in strings if '\u4e00' <= c <= '\u9fff' and ord(c) not in coverage}
    else:
        subset = (ROOT / 'lib/EpdFont/scripts/cn_i18n_chars.txt').read_text()
        missing = {c for c in strings if '\u4e00' <= c <= '\u9fff' and c not in subset}
    assert not missing, sorted(missing)
    with tempfile.TemporaryDirectory(prefix='reader-images-') as directory:
        work = Path(directory)
        (work / 'FakeDisk.h').write_text(r'''
#pragma once
#include <map>
#include <string>
inline std::map<std::string,std::string> disk;
inline bool failWrite=false, failRemove=false;
''')
        (work / 'HalStorage.h').write_text(r'''
#pragma once
#include "FakeDisk.h"
struct FakeStorage {
 bool exists(const char* path) const { return disk.contains(path); }
 bool remove(const char* path) { if(failRemove) return false; disk.erase(path); return true; }
};
inline FakeStorage Storage;
''')
        (work / 'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n')
        (work / 'PersistableStore.h').write_text(r'''
#pragma once
#include <ArduinoJson.h>
#include "FakeDisk.h"
struct PersistableStoreBase {
 static bool writeDocToFile(const char* p,const JsonDocument& doc) {
  if(failWrite) return false;
  std::string data;serializeJson(doc,data);disk[p]=data;return true;
 }
 static bool readDocFromFile(const char* p,JsonDocument& doc) {
  auto it=disk.find(p);return it!=disk.end() && !deserializeJson(doc,it->second);
 }
};
''')
        (work / 'CrossPointSettings.h').write_text(r'''
#pragma once
#include <cstdint>
struct FakeSettings {
 enum { IMAGE_SCALING_NEAREST=0, IMAGE_SCALING_BILINEAR=1 };
 int8_t readerImageContrast=0,readerImageBrightness=0;
 uint8_t imageScaling=1;
 bool saveOk=true;
 bool saveToFile() { return saveOk; }
};
using CrossPointSettings=FakeSettings;
inline FakeSettings SETTINGS;
''')
        settings_source = (ROOT / 'src/CrossPointSettings.cpp').read_text()
        start = settings_source.index('  const int savedImageBrightness =')
        end = settings_source.index('  const uint16_t spacingPercent =', start)
        load_brightness = settings_source[start:end]
        start = settings_source.index('  doc["readerImageBrightness"] =')
        end = settings_source.index('  doc["standbyCoverLayout"] =', start)
        save_brightness = settings_source[start:end]
        (work / 'BrightnessSettingsProbe.h').write_text(
            'void loadBrightness(JsonDocument& doc,int8_t& readerImageBrightness,bool& needsResave) {\n' + load_brightness + '}\n' +
            'void saveBrightness(JsonDocument& doc,int8_t readerImageBrightness) {\n' + save_brightness + '}\n')
        (work / 'test.cpp').write_text(r'''
#include "ReaderImageSettings.h"
#include "CrossPointSettings.h"
#include "ImageToneCurve.h"
#include "FakeDisk.h"
#include <ArduinoJson.h>
#include <cassert>
#include "BrightnessSettingsProbe.h"
int main() {
 ImageToneCurve curve;
 assert(curve.data()==nullptr);
 for(int c : {-100,-50,0,50,100}) for(int b : {-100,-40,0,40,100}) {
  curve.set(c,b);const auto* lut=curve.data();
  for(int i=1;i<256;++i) assert(!lut || lut[i]>=lut[i-1]);
 }
 curve.set(0,20);assert(curve.data()[64]>64 && curve.data()[255]==255);
 curve.set(0,-20);assert(curve.data()[64]<64 && curve.data()[0]==0);
 curve.set(50,0);assert(curve.data()[64]<64 && curve.data()[192]>192);
 curve.set(-50,0);assert(curve.data()[64]>64 && curve.data()[192]<192);
 // Stronger contrast must survive native 16-level quantization, not only
 // differ by sub-level Gray8 values. A narrow midpoint ramp used to change
 // at most one display level even at the highest setting.
 const auto level=[](int sample){return (sample+8)/17;};
 for (int c=-50;c<=50;++c) for(int b : {-40,0,40}) for(bool smooth : {false,true}) {
  ReaderImageProfile profile{static_cast<int8_t>(c),static_cast<int8_t>(b),smooth};
  curve.set(c,profile.renderBrightness());
  assert(profile.identity()==curve.identity(smooth));
  assert((curve.identity(smooth)&ImageToneCurve::kContrastAlgorithmIdentity)==(c ? ImageToneCurve::kContrastAlgorithmIdentity : 0));
  for(int sample=1;sample<256;++sample) assert(!curve.data() || curve.data()[sample]>=curve.data()[sample-1]);
 }
 curve.set(20,0);assert(curve.data()[112]==96 && curve.data()[144]==160);
 curve.set(30,0);assert(curve.data()[112]==80 && curve.data()[144]==176);
 curve.set(50,0);assert(curve.data()[112]==0 && curve.data()[144]==255);
 assert(level(curve.data()[132])-level(curve.data()[124])>=5);
 assert(level(curve.data()[144])-level(curve.data()[112])==15);
 assert((curve.identity(false)&ImageToneCurve::kContrastAlgorithmIdentity)!=0);
 curve.set(0,0);assert(curve.identity(false)==0 && curve.identity(true)==(1u<<16));
 // Brightness-only profiles retain their old cache identity and exact pixels.
 for(int b=-40;b<=40;++b) {
  curve.set(0,b);assert(curve.identity(false)==(uint32_t{static_cast<uint8_t>(b)}<<8));
  for(int i=0;i<256;++i) assert((curve.data()?curve.data()[i]:i)==std::clamp(i+b*255/100,0,255));
 }
 // Contrast does not invent tones in an already pure black/white source.
 curve.set(50,0);assert(curve.data()[0]==0 && curve.data()[255]==255);
 curve.set(0,0);assert(curve.data()==nullptr);
 // The rebased UI zero must produce exactly the old -15 LUT/cache key.
 ReaderImageProfile baseline{};
 assert(baseline.renderBrightness()==-15);
 curve.set(0,baseline.renderBrightness());
 for(int i=0;i<256;++i) assert(curve.data()[i]==std::clamp(i-38,0,255));
 assert(curve.identity(false)==baseline.identity());
 ReaderImageProfile original{0,15,false};
 curve.set(0,original.renderBrightness());assert(curve.data()==nullptr && original.identity()==0);
 curve.set(0,readerImageBrightness::toRender(-40));assert(curve.data()[255]==115);
 for(int legacy=-40;legacy<=25;++legacy) {
  if(legacy==0) continue;
  assert(readerImageBrightness::toRender(readerImageBrightness::fromLegacy(legacy))==legacy);
 }
 assert(readerImageBrightness::fromLegacy(0)==0 && readerImageBrightness::fromLegacy(-15)==0);
 assert(readerImageBrightness::fromLegacy(40)==40);
 // Run the production global-settings JSON loader/writer snippets, including
 // the resave request and no repeated scale conversion after persistence.
 for(int old : {-40,-15,0,10,40}) {
  JsonDocument settingsDoc;settingsDoc["readerImageBrightness"]=old;
  int8_t value=0;bool needsResave=false;loadBrightness(settingsDoc,value,needsResave);
  assert(needsResave && value==readerImageBrightness::fromLegacy(old));
  JsonDocument saved;saveBrightness(saved,value);
  assert(saved["readerImageBrightnessScaleVersion"]==2);
  int8_t reloaded=0;needsResave=false;loadBrightness(saved,reloaded,needsResave);
  assert(!needsResave && reloaded==value);
 }
 {JsonDocument missing;int8_t value=40;bool needs=false;loadBrightness(missing,value,needs);assert(value==0 && needs);}
 const std::string book="/books/图像.epub", other="/books/other.epub";
 auto loaded=readerImageSettings::load(book);
 assert(loaded.scope==ReaderImageScope::FollowDefault && loaded.profile.smooth);
 ReaderImageDraft draft{{25,-15,false},ReaderImageScope::Book};
 assert(readerImageSettings::save(book,draft));
 loaded=readerImageSettings::load(book);assert(loaded.profile==draft.profile);
 assert(loaded.scope==ReaderImageScope::Book);
 assert(readerImageSettings::defaults()==ReaderImageProfile(0,0,true));
 assert(readerImageSettings::load(other).scope==ReaderImageScope::FollowDefault);
 // Preview/cancel are drafts: changing the in-memory candidate cannot touch persisted values.
 draft.profile.contrast=-40;
 assert(readerImageSettings::load(book).profile.contrast==25);
 failWrite=true;assert(!readerImageSettings::save(book,draft));failWrite=false;
 assert(readerImageSettings::load(book).profile.contrast==25);
 draft.scope=ReaderImageScope::Default;
 SETTINGS.saveOk=false;
 assert(!readerImageSettings::save(book,draft));SETTINGS.saveOk=true;
 assert(readerImageSettings::defaults()==ReaderImageProfile(0,0,true));
 failRemove=true;assert(!readerImageSettings::save(book,draft));failRemove=false;
 assert(readerImageSettings::defaults()==ReaderImageProfile(0,0,true));
 assert(readerImageSettings::load(book).profile.contrast==25);
 assert(readerImageSettings::save(book,draft));
 assert(readerImageSettings::load(book).scope==ReaderImageScope::FollowDefault);
 assert(readerImageSettings::load(other).profile==draft.profile);
 // Explicit book overrides survive subsequent default changes.
 ReaderImageDraft explicitBook{{10,5,true},ReaderImageScope::Book};
 assert(readerImageSettings::save(book,explicitBook));
 draft.profile={-20,30,true};assert(readerImageSettings::save(other,draft));
 assert(readerImageSettings::load(book).profile==explicitBook.profile);
 ReaderImageDraft follow{draft.profile,ReaderImageScope::FollowDefault};
 failRemove=true;assert(!readerImageSettings::save(book,follow));failRemove=false;
 assert(readerImageSettings::load(book).profile==explicitBook.profile);
 assert(readerImageSettings::save(book,follow));
 assert(readerImageSettings::load(book).profile==draft.profile);
 // Corrupt/out-of-range input, and a mismatched full path at a hash collision, are safe.
 assert(readerImageSettings::save(book,explicitBook));
 assert(disk.size()==1);
 auto& raw=disk.begin()->second;JsonDocument doc;assert(!deserializeJson(doc,raw));
 assert(doc["version"]==2);
 const auto versionTwo=raw;
 for(int old : {-40,-15,0,10,40}) {
  doc["version"]=1;doc["brightness"]=old;raw.clear();serializeJson(doc,raw);
  const auto before=raw;
  loaded=readerImageSettings::load(book);
  assert(loaded.profile.brightness==readerImageBrightness::fromLegacy(old));
  assert(raw==before); // Loading/cancelling never rewrites a book profile.
  const auto migrated=loaded;
  assert(readerImageSettings::save(book,migrated));
  loaded=readerImageSettings::load(book);assert(loaded.profile==migrated.profile);
 }
 raw=versionTwo;assert(!deserializeJson(doc,raw));
 doc["contrast"]=123456;doc["brightness"]=-123456;raw.clear();serializeJson(doc,raw);
 loaded=readerImageSettings::load(book);assert(loaded.profile.contrast==50 && loaded.profile.brightness==-40);
 doc["book"]="/books/collision.epub";raw.clear();serializeJson(doc,raw);
 assert(readerImageSettings::load(book).scope==ReaderImageScope::FollowDefault);
 raw="broken";assert(readerImageSettings::load(book).scope==ReaderImageScope::FollowDefault);
}
''')
        subprocess.run([
            'clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
            '-I' + str(work), '-I' + str(JSON), '-I' + str(ROOT / 'src/activities/reader'),
            '-I' + str(ROOT / 'lib/Epub/Epub/converters'), '-I' + str(ROOT / 'lib/Epub'), '-I' + str(ROOT / 'src'),
            str(work / 'test.cpp'), str(ROOT / 'src/activities/reader/ReaderImageSettings.cpp'),
            '-o', str(work / 'test')], check=True)
        subprocess.run([str(work / 'test')], check=True)
    print('Reader image tone, book/default persistence and I/O failure cases passed (ASan/UBSan)')


if __name__ == '__main__':
    main()
