"""Compare every native font table and exercise boot allocation/validation failures."""
from pathlib import Path
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeFontMetadataTest(unittest.TestCase):
    def test_lossless_tables_oom_retry_and_corrupt_payload(self):
        subprocess.run(["python3", str(ROOT / "scripts/pack_metalio_font_metadata.py")], check=True)
        generated = ROOT / "generated/native-font-metadata"
        report = json.loads((generated / "packing-report.json").read_text())
        with tempfile.TemporaryDirectory(prefix="metalio-font-metadata-") as folder:
            work = Path(folder)
            (work / "Logging.h").write_text("#pragma once\n#define LOG_ERR(...) ((void)0)\n")
            (work / "esp_heap_caps.h").write_text(r'''
#pragma once
#include <cassert>
#include <cstdlib>
constexpr unsigned MALLOC_CAP_SPIRAM=1;
inline bool failAllocation=false;
inline unsigned allocations=0, releases=0;
inline void* heap_caps_malloc(size_t size,unsigned caps) {
  assert(caps==MALLOC_CAP_SPIRAM);
  if(failAllocation)return nullptr;
  ++allocations;return std::malloc(size);
}
inline void heap_caps_free(void* pointer) {++releases;std::free(pointer);}
''')
            faces = report["faces"]
            program = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <esp_heap_caps.h>
#include "EpdFontData.h"
#include "NativeFontMetadata.h"
#if RICKYOS_METALIO_FONT_METADATA
#include <nativefontmeta/metalio_body_10_regular.packed.h>
#include <nativefontmeta/metalio_book_10_medium.packed.h>
#else
#include <fonts/metalio_body_10_regular.packed.h>
#include <fonts/metalio_book_10_medium.packed.h>
#endif
uint64_t hash=1469598103934665603ULL;
void mix(uint64_t value){hash^=value;hash*=1099511628211ULL;}
void fingerprint(const EpdFontData& font,unsigned count) {
  mix(font.advanceY);mix(font.ascender);mix(font.descender);mix(font.intervalCount);
  for(unsigned i=0;i<count;++i){const auto& g=font.glyph[i];
    mix(g.width);mix(g.height);mix(g.advanceX);mix(g.left);mix(g.top);mix(g.dataLength);mix(g.dataOffset);
  }
  for(unsigned i=0;i<font.intervalCount;++i){const auto& interval=font.intervals[i];
    mix(interval.first);mix(interval.last);mix(interval.offset);
  }
}
int main(){
#if RICKYOS_METALIO_FONT_METADATA
  assert(!metalio_body_10_regular.glyph && metalio_body_10_regular.intervalCount==0);
  failAllocation=true;
  assert(!NativeFontMetadata::initialize(metalio_body_10_regular,metalio_book_10_medium));
  assert(allocations==0 && !metalio_body_10_regular.glyph);
  failAllocation=false;
#if TEST_CORRUPT
  assert(!NativeFontMetadata::initialize(metalio_body_10_regular,metalio_book_10_medium));
  assert(allocations==1 && releases==1);
  assert(!metalio_body_10_regular.glyph && metalio_body_10_regular.intervalCount==0);
  assert(!metalio_book_10_medium.glyph && metalio_book_10_medium.intervalCount==0);
  return 0;
#else
  assert(NativeFontMetadata::initialize(metalio_body_10_regular,metalio_book_10_medium));
  assert(NativeFontMetadata::initialize(metalio_body_10_regular,metalio_book_10_medium));
  assert(allocations==1 && releases==0);
  assert(metalio_body_10_regular.intervals==metalio_book_10_medium.intervals);
#endif
#endif
'''
            for face in faces:
                program += f'  fingerprint({face["name"]},{face["GlyphsCount"]});\n'
            program += '  std::cout<<hash<<"\\n";\n}\n'
            (work / "test.cpp").write_text(program)
            subprocess.run(["clang", "-c", str(ROOT / "lib/uzlib/src/tinflate.c"), "-o", str(work / "inflate.o")],
                           check=True)
            outputs = []
            for mode in ("baseline", "packed", "corrupt"):
                include = generated
                if mode == "corrupt":
                    include = work
                    data = (generated / "NativeFontMetadata.generated.inc").read_text()
                    # The first checksum changes, while the DEFLATE payload remains valid.
                    import re
                    data, count = re.subn(r"0x[0-9a-f]{8}U", "0x00000000U", data, count=1)
                    self.assertEqual(count, 1)
                    (work / "NativeFontMetadata.generated.inc").write_text(data)
                command = ["clang++", "-std=c++20", "-Wl,-dead_strip", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                           "-DRICKYOS_METALIO_FONT_METADATA=" + str(int(mode != "baseline")),
                           "-DTEST_CORRUPT=" + str(int(mode == "corrupt")),
                           "-I" + str(work), "-I" + str(include), "-I" + str(generated),
                           "-I" + str(ROOT / "generated"), "-I" + str(ROOT / "lib/EpdFont"),
                           "-I" + str(ROOT / "lib/uzlib/src"), str(work / "test.cpp"),
                           str(ROOT / "lib/EpdFont/NativeFontMetadata.cpp"), str(work / "inflate.o"),
                           "-o", str(work / mode)]
                subprocess.run(command, check=True)
                outputs.append(subprocess.check_output([str(work / mode)], text=True))
            self.assertEqual(outputs[0], outputs[1])


if __name__ == "__main__":
    unittest.main()
