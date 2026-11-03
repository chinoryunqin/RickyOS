"""Actual MOBI/KF8 cover extraction and loader routing; decoder interfaces are stubbed."""
from pathlib import Path
import struct
import subprocess
import tempfile

from test_plus_kindle_document import fixture
from test_plus_kindle_import import HAL

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT / "firmware/rickyos-plus-1.2/upstream"


def pdb(records):
    header = bytearray(78)
    header[60:68] = b'BOOKMOBI'
    struct.pack_into('>H', header, 76, len(records))
    table = bytearray()
    offset = 78 + len(records) * 8
    for record in records:
        table += struct.pack('>II', offset, 0)
        offset += len(record)
    return header + table + b''.join(records)


def header8(text, resource=3):
    exth = b'EXTH' + struct.pack('>II', 24, 1) + struct.pack('>III', 201, 12, 0)
    title = b'KF8 cover fixture'
    data = bytearray(264 + len(exth) + len(title))
    struct.pack_into('>HHIHHH', data, 0, 1, 0, len(text), 1, 4096, 0)
    data[16:20] = b'MOBI'
    for offset, value in {20:248,24:2,28:65001,36:8,84:264+len(exth),88:len(title),
                          108:resource,128:64,192:2,244:0xffffffff,248:0xffffffff,252:0xffffffff}.items():
        struct.pack_into('>I', data, offset, value)
    data[264:264+len(exth)] = exth
    data[264+len(exth):] = title
    return data


MAIN = r'''
#include "KindleImport.h"
#include "BookCoverLoader.h"
#include "HalStorage.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
namespace PanelTileCache {void forget(const std::string&){}void clear(){}}
std::string bytes(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 assert(argc==4);const std::string book=argv[1],out=argv[2],expected=argv[3];
 const std::string original=bytes(book);
 kindle::Error error;
 if(expected=="none"||expected=="encrypted"||expected=="unsupported"||expected=="invalid"){
  assert(!kindle::extractCover(book,out,error));
  if(expected=="none")assert(error==kindle::Error::None);
  if(expected=="encrypted")assert(error==kindle::Error::Encrypted);
  if(expected=="unsupported")assert(error==kindle::Error::Unsupported);
  if(expected=="invalid")assert(error==kindle::Error::Invalid);
  assert(!std::filesystem::exists(out));
  coverRoot=std::filesystem::path(out).parent_path().string();
  std::filesystem::remove_all(coverRoot+"/.crosspoint");
  BookCoverLoader::forgetDrawnCovers();
  assert(BookCoverLoader::canMakeThumbnail(book,220));
  assert(BookCoverLoader::ensureThumbnail(book,220).empty());
  assert(!std::filesystem::exists(coverRoot+"/.crosspoint/epub/thumb_220.bmp"));
  assert(BookCoverLoader::canMakeThumbnail(book,220)==(expected!="none"));
  return 0;
 }
 assert(kindle::extractCover(book,out,error));assert(error==kindle::Error::None);
 assert(bytes(out)==bytes(expected));assert(bytes(book)==original);
 std::filesystem::remove(out);
 writeCountdown=0;assert(!kindle::extractCover(book,out,error));assert(error==kindle::Error::Io);
 assert(!std::filesystem::exists(out));writeCountdown=-1;
 coverRoot=std::filesystem::path(out).parent_path().string();
 std::filesystem::remove_all(coverRoot+"/.crosspoint");
 BookCoverLoader::forgetDrawnCovers();
 assert(BookCoverLoader::canMakeThumbnail(book,220));
 bool generated=false;
 const auto thumb=BookCoverLoader::ensureThumbnail(book,220,&generated);
 assert(!thumb.empty()&&generated);assert(std::filesystem::exists(coverRoot+thumb));
 assert(!std::filesystem::exists(coverRoot+"/.crosspoint/epub/.kindle-cover.tmp"));
 assert(!std::filesystem::exists(coverRoot+"/.crosspoint/epub/kindle.epub"));
 generated=true;assert(BookCoverLoader::ensureThumbnail(book,220,&generated)==thumb&&!generated);
 assert(!BookCoverLoader::ensureFullCover(book).empty());
 // Transient write failure must not poison the next height with a coverless marker.
 writeCountdown=0;assert(BookCoverLoader::ensureThumbnail(book,230).empty());
 writeCountdown=-1;assert(BookCoverLoader::canMakeThumbnail(book,230));
 assert(!BookCoverLoader::ensureThumbnail(book,230).empty());
 // A user override has precedence over the source cover.
 {std::ofstream override(coverRoot+"/.crosspoint/epub/cover.override",std::ios::binary);
  const unsigned char png[]={0x89,'P','N','G',13,10,26,10,0};
  override.write(reinterpret_cast<const char*>(png),sizeof(png));}
 assert(!BookCoverLoader::ensureThumbnail(book,240).empty());
 assert(!std::filesystem::exists(coverRoot+"/.crosspoint/epub/.kindle-cover.tmp"));
 assert(bytes(book)==original);
}
'''


def run():
    with tempfile.TemporaryDirectory(prefix='kindle-covers-') as directory:
        work = Path(directory)
        hal = HAL.replace('inline int writeCountdown=-1;', 'inline int writeCountdown=-1;\ninline std::string coverRoot;\ninline std::string hostPath(const std::string& p){return p.starts_with("/.crosspoint")?coverRoot+p:p;}')
        hal = hal.replace('int read(void*p,size_t n)', 'int read(){char c;return f.get(c)?static_cast<unsigned char>(c):-1;}\n int read(void*p,size_t n)')
        hal = hal.replace('return f.open(p,false);', 'return f.open(hostPath(p),false);')
        hal = hal.replace('return f.open(p,true);', 'return f.open(hostPath(p),true);')
        hal = hal.replace('std::filesystem::exists(p)', 'std::filesystem::exists(hostPath(p))')
        hal = hal.replace('std::filesystem::remove(p)', 'std::filesystem::remove(hostPath(p))')
        hal = hal.replace(' public:\n bool openFileForRead', ' public:\n bool mkdir(const char*p){std::filesystem::create_directories(hostPath(p));return true;}\n bool openFileForRead')
        (work/'HalStorage.h').write_text(hal)
        stub = ROOT/'test/book_cover_loader/stubs'
        epub = (stub/'Epub.h').read_text().replace('  bool load(', '  bool probeCover(bool& declared)const{declared=true;return true;}\n  std::string getCachePath()const{return "/.crosspoint/epub";}\n  std::string getCoverOverridePath()const{return getCachePath()+"/cover.override";}\n  bool load(',1)
        (work/'Epub.h').write_text(epub)
        (work/'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n#define LOG_INF(...) ((void)0)\n#define LOG_DBG(...) ((void)0)\n')
        for name, prefix in [('JpegToBmpConverter','jpeg'),('PngToBmpConverter','png')]:
            (work/(name+'.h')).write_text('#pragma once\n#include <HalStorage.h>\nstruct '+name+'{\n'
                'static bool '+prefix+'FileToThumbBmpStreamWithSize(HalFile&,HalFile& out,int,int){return out.write(reinterpret_cast<const uint8_t*>("BM"),2)==2;}\n'
                'static bool '+prefix+'FileToCoverBmpStream(HalFile&,HalFile& out,bool,bool){return out.write(reinterpret_cast<const uint8_t*>("BM"),2)==2;}\n};\n')
        (work/'main.cpp').write_text(MAIN)
        exe=work/'check'
        subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                        '-DSIMULATOR=1','-DRICKYOS_KINDLE_IMPORT=1','-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer','-I'+str(work),'-I'+str(stub),
                        '-I'+str(PORT/'firmware/rickyos-plus-1.2/components/rickyos_application/port'),'-I'+str(ROOT/'lib/Memory'),
                        '-I'+str(ROOT/'lib/Epub'),'-I'+str(ROOT/'src/util'),
                        str(work/'main.cpp'),str(PORT/'firmware/rickyos-plus-1.2/components/rickyos_application/port/KindleDocument.cpp'),
                        str(PORT/'firmware/rickyos-plus-1.2/components/rickyos_application/port/KindleImport.cpp'),str(ROOT/'src/util/BookCoverLoader.cpp'),
                        str(ROOT/'lib/Epub/Epub/htmlEntities.cpp'),'-o',str(exe)],check=True)
        jpeg=b'\xff\xd8\xff'+bytes(range(256))*61
        png=b'\x89PNG\r\n\x1a\n'+bytes(range(256))*15
        for name,image in [('jpeg',jpeg),('png',png)]:
            expected=work/(name+'.image');expected.write_bytes(image)
            data=bytearray(fixture(b'<p>text</p>',extras=(image,),exth=((201,struct.pack('>I',0)),)))
            first=struct.unpack_from('>I',data,78)[0];struct.pack_into('>I',data,first+108,2)
            book=work/(name+'.MOBI');book.write_bytes(data)
            subprocess.run([str(exe),str(book),str(work/'cover.tmp'),str(expected)],check=True)
            text=b'<p>KF8</p>'
            fdst=b'FDST'+struct.pack('>IIII',12,1,0,len(text))
            azw=work/(name+'.AZW3');azw.write_bytes(pdb([header8(text),text,fdst,image]))
            subprocess.run([str(exe),str(azw),str(work/'cover.tmp'),str(expected)],check=True)
            # A combined MOBI6/KF8 file keeps absolute resources from the first header.
            original=fixture(b'legacy',exth=((121,struct.pack('>I',3)),(201,struct.pack('>I',0))))
            first=struct.unpack_from('>I',original,78)[0];end=struct.unpack_from('>I',original,86)[0]
            old=bytearray(original[first:end]);struct.pack_into('>I',old,108,2)
            combined=work/(name+'-combined.mobi');combined.write_bytes(pdb([old,b'legacy',image,header8(text),text,fdst]))
            subprocess.run([str(exe),str(combined),str(work/'cover.tmp'),str(expected)],check=True)
        for label in ('none','encrypted','unsupported','invalid'):
            data=bytearray(fixture(b'text',extras=(b'GIF89aXX',),exth=((201,struct.pack('>I',0)),)))
            first=struct.unpack_from('>I',data,78)[0];struct.pack_into('>I',data,first+108,2)
            if label=='none':data=bytearray(fixture(b'text'))
            if label=='encrypted':struct.pack_into('>H',data,first+12,1)
            if label=='invalid':struct.pack_into('>I',data,first+108,0xfffffffe)
            book=work/(label+'.mobi');book.write_bytes(data)
            subprocess.run([str(exe),str(book),str(work/'cover.tmp'),label],check=True)
    print('Kindle covers: 6 JPEG/PNG MOBI/KF8/combined routes, cache/retry checks and 4 refusal cases passed (ASan/UBSan).')


if __name__=='__main__':
    run()
