"""Render an explicitly supplied local font through the firmware's FreeType modules.

Usage: python3 tests/test_plus_cff_font.py FONT [UTF8_TEXT]
No proprietary book/font is stored in the repository. Optional UTF8_TEXT supplies
the codepoints to check; the default exercises ordinary and remapped Chinese.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile

PORT = Path(__file__).resolve().parents[1]
UP = Path(os.environ.get('RICKYOS_READER_SOURCE_ROOT', PORT / 'firmware/rickyos-plus-1.2/upstream'))
FONT = UP / 'freeink-sdk/libs/font/FreeInkFont'
EXTRA = PORT / 'firmware/rickyos-plus-1.2/components/rickyos_application/port/freetype-cff'
manifest = json.loads((EXTRA / 'provenance.json').read_text())
for name, sha in manifest['files'].items():
    expected = manifest.get('adaptations', {}).get(name, {}).get('ported_sha256', sha)
    assert hashlib.sha256((EXTRA / name).read_bytes()).hexdigest() == expected, name
font_path = Path(sys.argv[1]).resolve()
text = Path(sys.argv[2]).read_text() if len(sys.argv) > 2 else 'Hello 0123456789 AV fi 的䐜䠎图，。'

with tempfile.TemporaryDirectory(prefix='plus-cff-font-') as folder:
    p = Path(folder)
    # Send Unicode scalars to the native executable without a second UTF decoder.
    (p / 'codepoints.txt').write_text(' '.join(str(ord(c)) for c in sorted(set(text))))
    (p / 'test.cpp').write_text(r'''
#include <FtFont.h>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
struct alignas(std::max_align_t) Header {size_t bytes;};
size_t live=0,peak=0,largestRequest=0,failSize=0;unsigned rejected=0;
void* allocate(void*,size_t n){
 if(n>largestRequest)largestRequest=n;
 if(failSize&&n==failSize){++rejected;return nullptr;}
 auto* h=static_cast<Header*>(std::malloc(sizeof(Header)+n));
 if(!h)return nullptr;h->bytes=n;live+=n;if(live>peak)peak=live;return h+1;}
void release(void*,void* p){if(p){auto* h=static_cast<Header*>(p)-1;live-=h->bytes;std::free(h);}}
void* resize(void*ctx,void* p,size_t,size_t n){
 if(!n){release(ctx,p);return nullptr;}if(!p)return allocate(ctx,n);
 auto* old=static_cast<Header*>(p)-1;size_t oldBytes=old->bytes;
 auto* h=static_cast<Header*>(std::realloc(old,sizeof(Header)+n));if(!h)return nullptr;
 h->bytes=n;live=live-oldBytes+n;if(live>peak)peak=live;return h+1;
}
int main(int argc,char**argv){
 using freeink::font::FtFont;
 std::ifstream in(argv[1],std::ios::binary);assert(in);
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});assert(!bytes.empty());
 std::ifstream cps(argv[2]);std::vector<uint32_t> codepoints;uint32_t cp;
 while(cps>>cp)codepoints.push_back(cp);assert(!codepoints.empty());
 FtFont::MemoryCallbacks callbacks{nullptr,allocate,release,resize};assert(FtFont::configureMemory(&callbacks));
 size_t baseline=0,covered=0,rendered=0;uint64_t digest=1469598103934665603ULL;
 for(int round=0;round<3;++round){
  for(int size:{24,36,48}){
   FtFont font;assert(font.init(bytes.data(),bytes.size(),size));
   size_t count=0;
   for(auto cp:codepoints){
    if(!font.hasGlyph(cp))continue;++count;
    auto* glyph=font.rasterize(cp,size);assert(glyph);
    if(glyph->width&&glyph->height){assert(glyph->pixels);++rendered;
     for(size_t i=0;i<size_t(glyph->width)*glyph->height;++i){digest^=glyph->pixels[i];digest*=1099511628211ULL;}
    }
   }
   assert(count>0);covered=count;
  }
  if(round==0)baseline=live;else assert(live==baseline);
 }
 if(largestRequest>16000){
  // The largest per-call CFF workspace is allocated for each glyph. Failure
  // must return to the caller, leave the face reusable and release all data.
  FtFont font;assert(font.init(bytes.data(),bytes.size(),36));
  failSize=largestRequest;bool exercised=false;
  for(auto cp:codepoints){if(!font.hasGlyph(cp))continue;
   auto* glyph=font.rasterize(cp,36);
   if(rejected){assert(!glyph);exercised=true;break;}
  }
  failSize=0;
  if(exercised){for(auto cp:codepoints){if(font.hasGlyph(cp)){assert(font.rasterize(cp,36));break;}}}
 }
 assert(live==baseline);
 std::cout<<"covered="<<covered<<" bitmap_renders="<<rendered<<" engine_peak="<<peak
  <<" engine_retained="<<live<<" font_bytes="<<bytes.size()<<" largest_request="<<largestRequest<<" rejected_allocations="<<rejected<<" bitmap_digest="<<digest<<" three_cycles_stable=1\n";
}
''')
    enabled = '0' if os.environ.get('RICKYOS_CFF_TEST_BASELINE') == '1' else '1'
    flags = ['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-DFREEINK_FONT_ENABLE_AUTOHINT=1', '-DFREEINK_FONT_ENABLE_CFF=' + enabled, '-DFREEINK_FONT_ENABLE_PSNAMES=' + enabled]
    flags += ['-I' + str(FONT / 'include'), '-I' + str(FONT / 'src'),
              '-I' + str(FONT / 'third_party/freetype/include')]
    sources = sorted((FONT / 'src/freetype').glob('*.c'))
    if os.environ.get('RICKYOS_CFF_INTEGRATED') != '1':
        sources += sorted(EXTRA.glob('ft_*.c'))
    sources += [FONT / 'src/FontAlloc.c']
    objects = []
    for i, source in enumerate(sources):
        obj = p / f'{i}.o'
        subprocess.run(['clang', '-std=c11', *flags, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    sources_cpp = [p / 'test.cpp', FONT / 'src/FtFont.cpp', FONT / 'src/Gpos.cpp', FONT / 'src/Gsub.cpp']
    subprocess.run(['clang++', '-std=c++20', *flags, *map(str, sources_cpp), *objects,
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test'), str(font_path), str(p / 'codepoints.txt')], check=True)
