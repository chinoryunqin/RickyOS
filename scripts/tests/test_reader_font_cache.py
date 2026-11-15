"""Check retained reader/UI font groups and their ordinary-board LRU bound."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class ReaderFontCacheTest(unittest.TestCase):
    def test_page_reset_preserves_hot_groups_but_pressure_releases_them(self):
        source = (ROOT / 'lib/GfxRenderer/FontCacheManager.cpp').read_text()
        functions = '\n'.join(method(source, name) for name in (
            'void FontCacheManager::clearCache(', 'void FontCacheManager::releaseSdFontCaches('))
        program = r'''
#include <cassert>
#include <map>
struct FontDecompressor {
  int page=0,full=0;
  void clearCache(){++full;}
  void clearPageCache(){++page;}
};
struct Font {int page=0,full=0;void clearCache(){++page;}void releaseResidentCaches(){++full;}};
struct FontCacheManager {
  FontDecompressor* fontDecompressor_;
  std::map<int,Font*> sdCardFonts_;
  void clearCache();
  void releaseSdFontCaches(bool (*keepTtfFace)(int)=nullptr);
};
''' + functions + r'''
int main(){
  FontDecompressor groups;
  Font font;
  FontCacheManager f{&groups,{{7,&font}}};
  f.clearCache();f.clearCache();
  assert(font.page==2 && font.full==0);
#if FREEINK_DEVICE_METALIO_PLUS || FREEINK_DEVICE_METALIO_EINK4
  assert(groups.page==2 && groups.full==0);
#else
  assert(groups.page==0 && groups.full==2);
#endif
  f.releaseSdFontCaches();
  assert(font.full==1);
  assert(groups.full==((FREEINK_DEVICE_METALIO_PLUS || FREEINK_DEVICE_METALIO_EINK4)?1:3));
}
'''
        for plus, ordinary in ((0, 0), (1, 0), (0, 1)):
            with self.subTest(plus=plus, ordinary=ordinary):
                run_cpp(program, defines=(f'FREEINK_DEVICE_METALIO_PLUS={plus}',
                                          f'FREEINK_DEVICE_METALIO_EINK4={ordinary}'))

    def test_real_decompressor_retention_and_lru_on_ordinary(self):
        fixture = (ROOT / 'test/font_decompressor/FontDecompressorTest.cpp').read_text()
        fixture = fixture[:fixture.index('TEST(')].replace('#include <gtest/gtest.h>', '#include <cassert>\n#define EXPECT_EQ(a,b) assert((a)==(b))')
        fixture = fixture.replace('ASSERT_NE(bitmap, nullptr) << "glyph " << index;', 'assert(bitmap != nullptr);')
        fixture = fixture.replace('EXPECT_EQ(0, memcmp(bitmap, expected.data(), expected.size())) << "glyph " << index;',
                                  'assert(memcmp(bitmap, expected.data(), expected.size()) == 0);')
        program = fixture + r'''
} // namespace
int main(){
  FontDecompressor d;
  assert(d.init());
  auto second=makeFont([](const int i){return i/kPerGroup;},false);
  bind(second);
  constexpr unsigned slots=FREEINK_DEVICE_METALIO_EINK4?16:8;
  auto access=[&](unsigned group){
    expectGlyph(d,static_cast<int>(group%kGroups)*kPerGroup,group<kGroups?testFont():second);
  };
  for(unsigned i=0;i<slots;++i) access(i);
  assert(d.getStats().cacheMisses==slots);
  d.clearPageCache();
  access(0); // Page resets retain the hot groups and refresh LRU age.
  assert(d.getStats().cacheMisses==slots);
  access(slots); // One past the bound evicts group 1.
  assert(d.getStats().cacheMisses==slots+1);
  access(0);
  assert(d.getStats().cacheMisses==slots+1);
  access(1);
  assert(d.getStats().cacheMisses==slots+2);
  d.clearCache();
  access(0);
  assert(d.getStats().cacheMisses==slots+3);
}
'''
        includes = [ROOT / 'test/font_decompressor/stubs', ROOT / 'lib/EpdFont',
                    ROOT / 'lib/InflateReader', ROOT / 'lib/Utf8', ROOT / 'lib/uzlib/src']
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        with tempfile.TemporaryDirectory(prefix='reader-font-cache-') as directory:
            directory = Path(directory)
            cpp = directory / 'cache.cpp'
            cpp.write_text(program)
            obj = directory / 'tinflate.o'
            subprocess.run(['cc', *flags, '-I'+str(ROOT / 'lib/uzlib/src'), '-c',
                            str(ROOT / 'lib/uzlib/src/tinflate.c'), '-o', str(obj)], check=True)
            for ordinary in (0, 1):
                with self.subTest(ordinary=ordinary):
                    binary = directory / f'cache-{ordinary}'
                    subprocess.run(['c++', '-std=c++20', *flags, '-DRICKYOS_PRODUCT=1',
                                    f'-DFREEINK_DEVICE_METALIO_EINK4={ordinary}',
                                    *('-I'+str(p) for p in includes), str(cpp),
                                    str(ROOT / 'lib/EpdFont/FontDecompressor.cpp'),
                                    str(ROOT / 'lib/InflateReader/InflateReader.cpp'),
                                    str(ROOT / 'lib/Utf8/Utf8.cpp'), str(obj), '-lz', '-o', str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
