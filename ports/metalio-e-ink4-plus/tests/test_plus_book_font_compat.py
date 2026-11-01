"""Execute the actual CSS parser and BookFontSet, with bounded host-only font/I/O doubles."""
from pathlib import Path
import os
import subprocess
import tempfile

PORT = Path(__file__).resolve().parents[1]
UP = Path(os.environ.get('RICKYOS_READER_SOURCE_ROOT', PORT / 'firmware/rickyos-plus-1.2/upstream'))

with tempfile.TemporaryDirectory(prefix='plus-book-font-') as folder:
    p = Path(folder)
    # Same production bytes; only quoted platform headers resolve to host doubles.
    for name in ('BookFontSet.cpp', 'BookFontSet.h'):
        (p / name).write_bytes((UP / 'src/activities/reader' / name).read_bytes())
    (p / 'Arduino.h').write_text('''#pragma once
#include <cstdint>
inline unsigned long nowMs=1;
inline unsigned long millis(){return nowMs;}
struct EspHostStub{uint32_t getFreeHeap()const{return UINT32_MAX;}};
inline EspHostStub ESP;
''')
    (p / 'CrossPointSettings.h').write_text('''#pragma once
#include <cstdint>
struct Settings {uint8_t fontPointSize=18;};inline Settings settings;
#define SETTINGS settings
''')
    (p / 'HalMemory.h').write_text('''#pragma once
#include <cstddef>
struct Heap{size_t freeBytes;};inline size_t psramFree=16*1024*1024;
struct HalMemory{static Heap getPsramHeap(){return {psramFree};}};
''')
    (p / 'GfxRenderer.h').write_text('''#pragma once
#include <map>
class TtfEpdFont;
struct GfxRenderer{
 std::map<int,TtfEpdFont*> fonts;
 int getGrayscaleLevels()const{return 16;}
 void insertFont(int,int){} void registerTtfFont(int id,TtfEpdFont* f){fonts[id]=f;}
 void setFallbackFont(int,int){} void unregisterTtfFont(int id){fonts.erase(id);}
 void removeFont(int){}
};
''')
    (p / 'TtfEpdFont.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
inline unsigned loadAttempts=0;
struct TtfEpdFont{
 enum {Regular=0}; const uint8_t* data=nullptr;
 void addResidentSource(int,const uint8_t* p,uint32_t){data=p;}
 bool load(uint8_t,bool,size_t,uint16_t,bool){++loadAttempts;return data&&*data=='G';}
 void build(const char*){} int family()const{return 1;}
};
''')
    (p / 'Epub.h').write_text('''#pragma once
#include <Epub/css/CssParser.h>
#include <map>
#include <string>
#include <cstdlib>
#include <cstring>
struct Epub{
 CssParser* css;std::map<std::string,char> files; mutable unsigned reads=0;
 CssParser* getCssParser()const{return css;}
 bool getItemSize(const std::string& path,size_t* size)const{
  if(!files.count(path))return false;*size=128;return true;
 }
 uint8_t* readItemContentsToBytes(const std::string& path,size_t* size)const{
  if(!getItemSize(path,size))return nullptr;++reads;
  auto* p=static_cast<uint8_t*>(std::malloc(*size));if(p)std::memset(p,files.at(path),*size);return p;
 }
};
''')
    (p / 'test.cpp').write_text(r'''
#include "BookFontSet.h"
#include "Arduino.h"
#include "Epub.h"
#include "GfxRenderer.h"
#include "HalMemory.h"
#include "TtfEpdFont.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <string>
#include <iostream>
namespace fs=std::filesystem;
const char* sheet="OEBPS/Styles/fonts.css";
void parse(CssParser& p,const fs::path& dir,const std::string& text){
 std::ofstream(dir/"input.css",std::ios::binary)<<text;
 HalFile f;assert(f.open((dir/"input.css").c_str(),"rb"));
 assert(p.loadFromStream(f,sheet)==CssParser::ParseResult::Complete);
}
int main(int argc,char**argv){
 fs::path dir=argv[1];
 // BOM before either an at-rule or an ordinary selector; quotes and format()
 // must not become part of the archive path. Retain ordered supported sources.
 const std::string body="p {font-family: hidden;}";
 std::string css="\xEF\xBB\xBF@font-face {src:url(no.eot);src:url(no.woff) format('woff'),"
   "url('../Fonts/missing.ttf') format('truetype'),"
   "url('../Fonts/:*:real.otf') format('opentype');font-family:hidden;}"+body;
 CssParser parser(dir.string());parse(parser,dir,css);
 assert(parser.fontFaceCount()==2);
 assert(parser.fontFaceAt(0).path=="OEBPS/Fonts/missing.ttf");
 assert(parser.fontFaceAt(1).path=="OEBPS/Fonts/:*:real.otf");
 assert(parser.familyAt(parser.resolveStyle("p","").fontFamily)=="hidden");
 Epub epub{&parser,{{"OEBPS/Fonts/:*:real.otf",'G'}}};GfxRenderer renderer;
 {BookFontSet fonts(renderer);fonts.open(epub);auto face=fonts.faceForFamily("hidden");
  assert(face!=0);assert(epub.reads==0); // metadata selection never eagerly reads font bytes
  auto id=fonts.fontIdFor(face,7,0);assert(id!=7&&epub.reads==1&&renderer.fonts.size()==1);
  assert(fonts.fontIdFor(face,7,0)==id&&epub.reads==1);
 }assert(renderer.fonts.empty());
 // A malformed first SFNT falls through without changing the saved face slot.
 CssParser fallback((dir/"fallback").string());
 parse(fallback,dir,"@font-face{font-family: hidden;src:url(../Fonts/bad.ttf),url(../Fonts/good.otf);}"
   "@font-face{font-family: other;src:url(../Fonts/other.ttf);}"+body);
 Epub e2{&fallback,{{"OEBPS/Fonts/bad.ttf",'B'},{"OEBPS/Fonts/good.otf",'G'},
   {"OEBPS/Fonts/other.ttf",'G'}}};
 {BookFontSet fonts(renderer);fonts.open(e2);auto face=fonts.faceForFamily("hidden");
  auto id=fonts.fontIdFor(face,7,0);assert(id!=7&&e2.reads==2);
  assert(fonts.fontIdFor(face,7,0)==id&&e2.reads==2);
  assert(fonts.faceForFamily("unknown, other")!=0);
 }assert(renderer.fonts.empty());
 // Memory pressure keeps the existing reserve/backoff and does not iterate
 // through more font files to evade the gate.
 psramFree=0;Epub e3{&fallback,e2.files};
 {BookFontSet fonts(renderer);fonts.open(e3);auto face=fonts.faceForFamily("hidden");
  assert(fonts.fontIdFor(face,7,0)==7&&e3.reads==0);
  psramFree=16*1024*1024;assert(fonts.fontIdFor(face,7,0)==7&&e3.reads==0);
  nowMs+=BookFontSet::kRetryAfterMs;
  assert(fonts.fontIdFor(face,7,0)!=7&&e3.reads==2);
 }
 // Protocol-relative/remote/root-escaping resources never become local fonts.
 CssParser unsafe((dir/"unsafe").string());
 parse(unsafe,dir,"@font-face{font-family: web;src:url(//host/a.ttf),url(https://host/b.otf),"
   "url(data:font/ttf;base64,AAAA),url(../../../escape.ttf),url(file:///a.ttf);}"+body);
 assert(unsafe.fontFaceCount()==0);

 // Many device-specific missing paths must not crowd out the actual book
 // families declared near the end (the Jian Ke regression).
 CssParser archive((dir/"archive").string());std::string placeholders;
 for(int family=0;family<12;++family){
  placeholders+="@font-face{font-family:f"+std::to_string(family)+";src:local(\"宋体\"),";
  for(int candidate=0;candidate<20;++candidate){if(candidate)placeholders+=',';
   placeholders+="url(../device"+std::to_string(candidate)+"/absent.ttf)";}
  placeholders+=";}";
 }
 placeholders+="@font-face{font-family:actual;src:url(../Fonts/actual.ttf);}"+body;
 {std::ofstream(dir/"archive.css")<<placeholders;}
 HalFile archiveFile;assert(archiveFile.open((dir/"archive.css").c_str(),"rb"));
 unsigned checked=0;
 assert(archive.loadFromStream(archiveFile,sheet,
  [](const void* ctx,const std::string& path){++*static_cast<unsigned*>(const_cast<void*>(ctx));
   return path=="OEBPS/Fonts/actual.ttf";},&checked)==CssParser::ParseResult::Complete);
 assert(checked==241&&archive.fontFaceCount()==13&&archive.fontFaceAt(12).family=="actual");
 fs::create_directories(dir/"archive");assert(archive.saveToCache(true));CssParser archiveCached((dir/"archive").string());
 assert(archiveCached.loadFromCache()==CssParser::CacheLoadResult::Complete);
 assert(archiveCached.fontFaceCount()==13);

 // Per-rule and global stores remain bounded even for self-made source lists.
 CssParser bounded((dir/"bounded").string());std::string many="@font-face{font-family:x;src:";
 for(int i=0;i<40;++i){if(i)many+=',';many+="url(../Fonts/f"+std::to_string(i)+".ttf)";}
 parse(bounded,dir,many+";}"+body);assert(bounded.fontFaceCount()==8);
 CssParser ordinary((dir/"ordinary").string());parse(ordinary,dir,"\xEF\xBB\xBFp{font-weight:bold;}");
 assert(ordinary.resolveStyle("p","").fontWeight==CssFontWeight::Bold);
 assert(parser.saveToCache(true));CssParser cached(dir.string());
 assert(cached.loadFromCache()==CssParser::CacheLoadResult::Complete);
 assert(cached.fontFaceCount()==2&&cached.fontFaceAt(1).path=="OEBPS/Fonts/:*:real.otf");

 // Background URLs use a separate bounded pool and cascade independently of fonts.
 CssParser backgrounds((dir/"backgrounds").string());
 parse(backgrounds,dir,"body{background-image:url('../Images/人物.webp');}"
   "body.none{background-image:none;} body.short{background: url(../Images/art.png) top no-repeat;}"
   "body.remote{background-image:url(https://host/x.webp);} p{font-family:hidden;}");
 auto bg=backgrounds.resolveStyle("body","");assert(bg.hasBackgroundImage());
 assert(backgrounds.backgroundImageAt(bg.backgroundImage)=="OEBPS/Images/人物.webp");
 auto noBg=backgrounds.resolveStyle("body","none");assert(noBg.hasBackgroundImage()&&noBg.backgroundImage==0);
 auto shortBg=backgrounds.resolveStyle("body","short");
 assert(backgrounds.backgroundImageAt(shortBg.backgroundImage)=="OEBPS/Images/art.png");
 auto inlineBg=backgrounds.parseInlineStyleWithFamilies("background-image:url('../Images/inline.webp')", "OEBPS/Text/");
 assert(backgrounds.backgroundImageAt(inlineBg.backgroundImage)=="OEBPS/Images/inline.webp");
 assert(backgrounds.familyAt(backgrounds.resolveStyle("p","").fontFamily)=="hidden");
 fs::create_directories(dir/"backgrounds");assert(backgrounds.saveToCache(true));
 CssParser backgroundsCached((dir/"backgrounds").string());
 assert(backgroundsCached.loadFromCache()==CssParser::CacheLoadResult::Complete);
 assert(backgroundsCached.backgroundImageAt(backgroundsCached.resolveStyle("body","").backgroundImage)=="OEBPS/Images/人物.webp");
 assert(backgrounds.saveToCache(false));assert(backgroundsCached.inspectCache()==CssParser::CacheStatus::Partial);
 // Exhausting art cannot evict the already recorded font-family list.
 std::string art;for(int i=0;i<40;++i)art+=".a"+std::to_string(i)+"{background-image:url(../Images/"+std::to_string(i)+".png);}";
 {std::ofstream(dir/"many.css")<<art+"p.later{font-family:later;}";}HalFile artFile;assert(artFile.open((dir/"many.css").c_str(),"rb"));
 assert(backgrounds.loadFromStream(artFile,sheet)==CssParser::ParseResult::Partial);
 assert(backgrounds.familyAt(backgrounds.resolveStyle("p","").fontFamily)=="hidden");

 assert(backgrounds.familyAt(backgrounds.resolveStyle("p","later").fontFamily)=="later");
 // Reject an old semantic cache without touching reading progress.
 std::ofstream(dir/"progress.bin",std::ios::binary)<<"KEEP";
 {std::fstream f(dir/"css_rules.cache",std::ios::binary|std::ios::in|std::ios::out);f.put(15);}
 CssParser stale(dir.string());assert(stale.loadFromCache()==CssParser::CacheLoadResult::Invalid);
 std::ifstream progress(dir/"progress.bin");std::string value;progress>>value;assert(value=="KEEP");
 std::cout<<"BOM, ordered SFNT candidates, ZIP paths, lazy load, malformed-font fallback, "
  "memory backoff, resource rejection, bounds, cache migration and cleanup passed\n";
}
''')
    includes = [p, UP / 'test/css_parser/stubs', UP / 'lib/Epub', UP / 'lib/EpdFont', UP / 'lib/Memory']
    command = ['clang++', '-std=c++20', '-DBOARD_HAS_PSRAM', '-DSIMULATOR',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    command += ['-I' + str(path) for path in includes]
    command += [str(p / 'test.cpp'), str(p / 'BookFontSet.cpp'),
                str(UP / 'lib/Epub/Epub/css/CssParser.cpp'), str(UP / 'lib/Epub/Epub/BookFonts.cpp'),
                '-o', str(p / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(p / 'test'), str(p)], check=True)
