"""Compile product theme guards and retain upstream theme/setting compatibility."""
import configparser
from pathlib import Path
import re
import subprocess
import sys
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class RickyFixedThemeTest(unittest.TestCase):
    def test_theme_selection_and_reload_always_use_one_product_instance(self):
        source = (ROOT / 'src/components/UITheme.cpp').read_text()
        enum = re.search(r'enum UI_THEME \{.*?\};',
                         (ROOT / 'src/CrossPointSettings.h').read_text(), re.S).group()
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
enum class InxRecentLayout : uint8_t {Flow,Grid,List,Icons,Cover,Count};
enum class InxItemLayout : uint8_t {Icons,List,Count};
struct CrossPointSettings {
 ENUM
 static constexpr uint8_t INX_TAB_BOTTOM=1;
 uint8_t uiTheme=0,inxRecentLayout=0,inxLibraryLayout=0,inxAppsLayout=0,inxTabPosition=0;
 void enforceProductLayout();
} SETTINGS;
struct ThemeMetrics {};
struct BaseTheme {};
struct InxTheme : BaseTheme {};
namespace InxMetrics {const ThemeMetrics values{};}
struct Font {const int* data=nullptr;};
const int notosans_16_regular=1,notosans_16_bold=2;
Font ui18RegularFont,ui18BoldFont;
struct CoverGridHomeUi {};
struct UITheme {
 InxTheme fixedTheme;
 BaseTheme* currentTheme=nullptr;
 const ThemeMetrics* currentMetrics=nullptr;
 CrossPointSettings::UI_THEME currentType=CrossPointSettings::CLASSIC;
 bool metricsValid=true;
 static bool supportsCoverGrid(); static bool hasCoverGridHome();
 static void drawCoverGridHome(CoverGridHomeUi&);
 void setTheme(CrossPointSettings::UI_THEME); void reload();
};
'''.replace('ENUM', enum) + method((ROOT / 'src/CrossPointSettings.cpp').read_text(),
                                  'void CrossPointSettings::enforceProductLayout(') + '\n'.join(method(source, marker) for marker in (
            'bool UITheme::supportsCoverGrid(', 'bool UITheme::hasCoverGridHome(',
            'void UITheme::drawCoverGridHome(', 'void UITheme::setTheme(', 'void UITheme::reload(')) + r'''
int main() {
 UITheme ui;
 // No alternative theme class, HalMemory or allocator exists in this harness.
 // All persisted IDs (including corrupt values) must take the fixed path.
 for(int saved=0;saved<256;++saved) {
  SETTINGS.uiTheme=saved;
  SETTINGS.inxRecentLayout=SETTINGS.inxLibraryLayout=SETTINGS.inxAppsLayout=SETTINGS.inxTabPosition=saved;
  ui.setTheme(static_cast<CrossPointSettings::UI_THEME>(saved));
  assert(SETTINGS.uiTheme==CrossPointSettings::INX);
  assert(SETTINGS.inxRecentLayout==0 && SETTINGS.inxLibraryLayout==0 && SETTINGS.inxAppsLayout==0 && SETTINGS.inxTabPosition==1);
  assert(ui.currentTheme==&ui.fixedTheme && ui.currentType==CrossPointSettings::INX);
  assert(ui.currentMetrics==&InxMetrics::values && !ui.metricsValid);
  SETTINGS.uiTheme=saved;
  SETTINGS.inxRecentLayout=SETTINGS.inxLibraryLayout=SETTINGS.inxAppsLayout=SETTINGS.inxTabPosition=saved;
  ui.metricsValid=true; ui.reload();
  assert(SETTINGS.uiTheme==CrossPointSettings::INX && ui.currentTheme==&ui.fixedTheme);
  assert(SETTINGS.inxRecentLayout==0 && SETTINGS.inxLibraryLayout==0 && SETTINGS.inxAppsLayout==0 && SETTINGS.inxTabPosition==1);
  assert(!UITheme::supportsCoverGrid() && !UITheme::hasCoverGridHome());
  assert(ui18RegularFont.data==&notosans_16_regular && ui18BoldFont.data==&notosans_16_bold);
 }
 CoverGridHomeUi unused; UITheme::drawCoverGridHome(unused);
}
''', defines=('RICKYOS_PRODUCT', 'CROSSMUX_UI_PROFILE_HIGH_DPI'))

    def test_mutable_theme_row_removed_from_shared_settings_catalog_only_for_product(self):
        source = (ROOT / 'src/SettingsList.h').read_text()
        values = method(source, 'inline std::vector<StrId> homeThemeValues(')
        start = source.index('#ifndef RICKYOS_PRODUCT\n        SettingInfo::Enum(StrId::STR_UI_THEME')
        entry = source[start:source.index('#endif', start) + len('#endif')]
        labels = sorted(set(re.findall(r'StrId::(\w+)', values + entry)) - {'STR_UI_THEME', 'STR_CAT_DISPLAY'})
        program = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <iterator>
#include <cstring>
enum class StrId {STR_UI_THEME,STR_CAT_DISPLAY,LABELS};
struct CrossPointSettings {uint8_t uiTheme=5,inxRecentLayout=0,inxLibraryLayout=0,inxAppsLayout=0,inxTabPosition=1;};
struct UITheme {static bool supportsCoverGrid(){return true;}};
struct SettingInfo {
 const char* key;
 static SettingInfo Enum(StrId,uint8_t CrossPointSettings::*,std::vector<StrId>,const char* k,StrId){return {k};}
};
#ifndef RICKYOS_PRODUCT
VALUES
#endif
int main() {
 const std::vector<SettingInfo> catalog={
ENTRY
 };
#ifdef RICKYOS_PRODUCT
 assert(catalog.empty());
#else
 const char* expected[]={"uiTheme","inxRecentLayout","inxLibraryLayout","inxAppsLayout","inxTabPosition"};
 assert(catalog.size()==5);
 for(unsigned i=0;i<5;++i) assert(std::strcmp(catalog[i].key,expected[i])==0);
 assert(homeThemeValues().size()==7);
#endif
}
'''.replace('LABELS', ','.join(labels)).replace('VALUES', values).replace('ENTRY', entry)
        for defines in ((), ('RICKYOS_PRODUCT',)):
            run_cpp(program, defines=defines)

    def test_legacy_json_theme_is_normalized_and_resaved_without_resetting_other_preferences(self):
        source = (ROOT / 'src/CrossPointSettings.cpp').read_text()
        loading = method(source, 'bool CrossPointSettings::fromJson(')
        start = loading.index('  uiTheme = INX;')
        migration = loading[start:loading.index('  copyToField(rickyNickname', start)]
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
constexpr uint8_t INX=5;
struct Value {
 int n; bool null=false;
 bool isNull()const{return null;}
 template<class T>bool is()const{return !null && n>=0 && n<=255;}
 template<class T>T as()const{return static_cast<T>(n);}
};
struct Doc {Value value; Value operator[](const char* key)const{assert(std::strcmp(key,"uiTheme")==0);return value;}};
void load(Doc doc,uint8_t& uiTheme,bool& needsResave) { MIGRATION }
int main() {
 for(int saved=-1;saved<=256;++saved) {
  uint8_t uiTheme=0; bool needsResave=false;
  load({{saved}},uiTheme,needsResave);
  assert(uiTheme==INX && needsResave==(saved!=INX));
 }
 uint8_t uiTheme=0;bool needsResave=false;
 load({{0,true}},uiTheme,needsResave);assert(uiTheme==INX && !needsResave);
}
'''.replace('MIGRATION', migration))
        saving = method(source, 'void CrossPointSettings::toJson(')
        self.assertIn('doc["uiTheme"] = static_cast<uint8_t>(INX)', saving)
        legacy = method(source, 'bool CrossPointSettings::loadFromBinaryFile(')
        self.assertIn('#ifdef RICKYOS_PRODUCT\n  uiTheme = INX;\n  enforceProductLayout();\n#else\n  uiTheme = value(20, uiTheme);', legacy)
        self.assertIn('doc["rickyHomePhrase"] = rickyHomePhrase', saving)

    def test_all_layout_json_values_are_pinned_and_other_preferences_untouched(self):
        source = (ROOT / 'src/CrossPointSettings.cpp').read_text()
        loading = method(source, 'bool CrossPointSettings::fromJson(')
        start = loading.index('  struct FixedLayout {')
        migration = loading[start:loading.index('  enforceProductLayout();', start) + len('  enforceProductLayout();')]
        saving = method(source, 'void CrossPointSettings::toJson(')
        start = saving.index('  doc["inxRecentLayout"]')
        save = saving[start:saving.index('  doc["rickyNickname"]', start)]
        program = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
enum class InxRecentLayout : uint8_t {Flow,Grid,List,Icons,Cover,Count};
enum class InxItemLayout : uint8_t {Icons,List,Count};
struct Value {
 int n;bool null=false;
 bool isNull()const{return null;}
 template<class T>bool is()const{return !null && n>=0 && n<=255;}
 template<class T>T as()const{return static_cast<T>(n);}
 Value& operator=(uint8_t value){n=value;null=false;return *this;}
};
struct Doc {
 Value values[4];
 Value& operator[](const char* key){
  const char* keys[]={"inxRecentLayout","inxLibraryLayout","inxAppsLayout","inxTabPosition"};
  for(unsigned i=0;i<4;++i) if(std::strcmp(key,keys[i])==0) return values[i];
  assert(false);return values[0];
 }
};
struct CrossPointSettings {
 static constexpr uint8_t INX=5,INX_TAB_BOTTOM=1;
 uint8_t uiTheme=0,inxRecentLayout=2,inxLibraryLayout=1,inxAppsLayout=1,inxTabPosition=0;
 int readerFontSize=18;
 const char* phrase="My phrase";
 void enforceProductLayout();
 bool load(Doc doc){bool needsResave=false; MIGRATION return needsResave;}
 void save(Doc& doc)const {SAVE}
};
NORMALIZE
int main() {
 for(unsigned key=0;key<4;++key) for(int value=-1;value<=256;++value) {
  CrossPointSettings s;
  Doc doc{{{0},{0},{0},{1}}};
  doc.values[key].n=value;
  assert(s.load(doc)==(value!=(key==3?1:0)));
  assert(s.uiTheme==5 && s.inxRecentLayout==0 && s.inxLibraryLayout==0 && s.inxAppsLayout==0 && s.inxTabPosition==1);
  assert(s.readerFontSize==18 && std::strcmp(s.phrase,"My phrase")==0);
  // Saving cannot export an alternate layout, even if a field was mutated in RAM.
  s.inxRecentLayout=s.inxLibraryLayout=s.inxAppsLayout=s.inxTabPosition=255;
  s.save(doc);
  for(unsigned i=0;i<4;++i) assert(doc.values[i].n==(i==3?1:0));
 }
 CrossPointSettings s;Doc missing{{{0,true},{0,true},{0,true},{0,true}}};
 assert(!s.load(missing) && s.inxTabPosition==1 && s.inxRecentLayout==0);
}
'''.replace('MIGRATION', migration).replace('SAVE', save).replace('NORMALIZE', method(source, 'void CrossPointSettings::enforceProductLayout('))
        run_cpp(program)
        catalog = (ROOT / 'src/SettingsList.h').read_text()
        guarded = catalog[catalog.index('#ifndef RICKYOS_PRODUCT\n        SettingInfo::Enum(StrId::STR_UI_THEME'):]
        guarded = guarded[:guarded.index('#endif')]
        for key in ('inxRecentLayout', 'inxLibraryLayout', 'inxAppsLayout', 'inxTabPosition'):
            self.assertIn('"' + key + '"', guarded)
        self.assertNotIn('fontSize', guarded)
        self.assertNotIn('fontFamily', guarded)

    def test_only_standalone_alternative_sources_are_excluded_in_product(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / 'platformio.ini')
        excludes = config['rickyos_app_profile']['build_src_filter']
        for path in ('components/themes/lyra/Lyra3CoversTheme.cpp',
                     'components/themes/lyra/LyraCarouselTheme.cpp', 'components/themes/roundedraff/'):
            self.assertIn('-<' + path + '>', excludes)
        for path in ('components/themes/lyra/LyraTheme.cpp', 'components/themes/BaseTheme.cpp',
                     'components/themes/inx/'):
            self.assertNotIn('-<' + path + '>', excludes)
        for env in ('env:rickyos_readpico', 'env:simulator_rickyos'):
            self.assertIn('${rickyos_app_profile.build_src_filter}', config[env]['build_src_filter'])

    def test_stock_theme_capability_saved_ids_and_oom_fallback_unchanged(self):
        subprocess.run([sys.executable, str(ROOT / 'test/inx_navigation/test_cover_grid.py')], check=True)


if __name__ == '__main__':
    unittest.main()
