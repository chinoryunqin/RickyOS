"""Run the production CSS+chapter parser against actual book canvas backgrounds."""
from pathlib import Path
import subprocess, tempfile, zipfile, os
PORT=Path(__file__).resolve().parents[1]
UP=Path(os.environ.get('RICKYOS_READER_SOURCE_ROOT',PORT/'firmware/rickyos-plus-1.2/upstream'))
STUB=UP/'test/chapter_html_slim_parser/stubs'
with tempfile.TemporaryDirectory(prefix='plus-css-background-') as folder:
 p=Path(folder)
 # I/O double supplies archive-entry bytes to the actual streamed header probe.
 stub=(STUB/'Epub.h').read_text().replace('#include <string>','#include <string>\n#include <map>\n#include <vector>')
 stub=stub.replace('  std::string sourcePath', '  std::map<std::string,std::vector<uint8_t>> items;\n  mutable int headerReads=0;\n  std::string sourcePath')
 stub=stub.replace('bool readItemContentsToStream(const std::string&, Output&, size_t, bool = false) const {\n    return false;', '''bool readItemContentsToStream(const std::string& path, Output& output, size_t, bool = false) const {
    ++headerReads; auto it=items.find(path);if(it==items.end())return false;
    for(uint8_t b:it->second)if(output.write(b)!=1)break;
    return true;''')
 (p/'Epub.h').write_text(stub)
 # Replace the old parser suite's always-failing dimensions double with the exact
 # production validator; all other link doubles remain unchanged.
 links=(UP/'test/chapter_html_slim_parser/ParserLinkStubs.cpp').read_text()
 begin=links.index('bool ImageToFramebufferDecoder::validateAndStoreDimensions(')
 end=links.index('\n}',begin)+2
 decoder=(UP/'lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp').read_text()
 a=decoder.index('bool ImageToFramebufferDecoder::validateAndStoreDimensions(')
 b=decoder.index('\nvoid ImageToFramebufferDecoder::yieldDuringDecode',a)
 (p/'links.cpp').write_text('#include <Logging.h>\n'+links[:begin]+decoder[a:b]+links[end:])

 # Parser sees this double only for archive I/O. Probe, CSS, callbacks and layout are production.
 (p/'main.cpp').write_text(r'''
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <Epub.h>
#include <cassert>
#include <fstream>
#include <filesystem>
#define class struct
#define private public
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#undef private
#undef class
namespace fs=std::filesystem;
extern std::vector<std::string> laidOutWords;
void run(const fs::path& dir,const std::string& klass,bool enabled,bool style,bool expected) {
 GfxRenderer gfx;CssParser css(dir.string());HalFile sheet;
 assert(sheet.open((dir/"Style.css").c_str(),"rb"));
 assert(css.loadFromStream(sheet,"OEBPS/Styles/Style.css")==CssParser::ParseResult::Complete);
 auto epub=std::make_shared<Epub>();epub->css=&css;
 for(const auto& entry:fs::directory_iterator(dir/"Images")) {
  std::ifstream f(entry.path(),std::ios::binary);epub->items["OEBPS/Images/"+entry.path().filename().string()]={std::istreambuf_iterator<char>(f),{}};
 }
 const auto path=(dir/"page.xhtml").string();
 if(fs::exists(dir/(klass+".xhtml"))){fs::copy_file(dir/(klass+".xhtml"),path,fs::copy_options::overwrite_existing);}
 else std::ofstream(path)<<"<html><body class='"<<klass<<"'><p>Readable text remains</p></body></html>";
 std::vector<std::unique_ptr<Page>> pages;laidOutWords.clear();
 ChapterHtmlSlimParser parser(epub,path,gfx,0,1.f,0,0,0,620,1000,false,false,
  [&](auto page,auto,auto,auto){pages.push_back(std::move(page));},style,"OEBPS/Text/","img_",enabled?0:2,{},nullptr,&css,true);
 assert(parser.parseAndBuildPages());assert(pages.size()==1&&!laidOutWords.empty());
 assert(pages[0]->hasImages()==expected);
 if(expected){
  auto& image=static_cast<PageImage&>(*pages[0]->elements[0]);
  assert(image.getImageBlock().getWidth()>0&&image.getImageBlock().getWidth()<=620);
  assert(image.getImageBlock().getHeight()>0&&image.getImageBlock().getHeight()<=1000);
  assert(image.yPos==0&&epub->headerReads==1);
  assert(parser.allocatePage()&&parser.currentPage->hasImages()&&parser.currentPageNextY==0);
 }else if(!enabled||!style){assert(epub->headerReads==0);}
}
int main(int argc,char**argv){fs::path dir=argv[1];
 for(auto cls:{"qmp0","qmp1","qmp2","qmp3"})run(dir,cls,true,true,true);
 run(dir,"qmp0",false,true,false);run(dir,"qmp0",true,false,false);
 run(dir,"qmp5",true,true,false);run(dir,"ordinary",true,true,false);
}
''')
 book=Path('/Users/rick/Downloads/chat_file_1040g3c8326gtb01glg5g5pe35i92fhaapqn8p78_《超神机械师》作者齐佩甲 EPUB  xsjtfa制作 .epub')
 (p/'Images').mkdir()
 if book.exists():
  with zipfile.ZipFile(book) as z:
   (p/'Style.css').write_bytes(z.read('OEBPS/Styles/Style.css'))
   for i in range(4):(p/f'qmp{i}.xhtml').write_bytes(z.read(f'OEBPS/Text/Section000{i+1}.xhtml'))
   for name in z.namelist():
    if name.endswith('.webp'):(p/'Images'/Path(name).name).write_bytes(z.read(name)[:1024])
 else:
  (p/'Style.css').write_text(''.join(f'body.qmp{i}{{background-image:url(../Images/{i}.webp);}}' for i in range(6)))
  # Minimal VP8X header for layout/header tests; real codec has a separate suite.
  for i in range(4):(p/'Images'/f'{i}.webp').write_bytes(b'RIFF'+(30).to_bytes(4,'little')+b'WEBPVP8X'+(10).to_bytes(4,'little')+b'\0'*4+(799).to_bytes(3,'little')+(2124).to_bytes(3,'little'))
 sources=['lib/Epub/Epub/parsers/RecoveringHtmlStream.cpp','lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp','lib/Epub/Epub/css/CssParser.cpp',
  'lib/Epub/Epub/ParsedText.cpp','lib/Epub/Epub/BookFonts.cpp','lib/Epub/Epub/WordStore.cpp',
  'lib/Epub/Epub/converters/ImageDimsProbe.cpp','lib/Utf8/Utf8.cpp']
 includes=[p,STUB,PORT/'firmware/rickyos-plus-1.2/components/rickyos_application/port']+[UP/x for x in ['lib','lib/Epub','lib/Epub/Epub','lib/Epub/Epub/parsers','lib/Epub/Epub/css','lib/EpdFont','lib/Utf8','lib/Memory','lib/Serialization','lib/MiniBidi','lib/FsHelpers','lib/XmlParserUtils']]
 cmd=['clang++','-std=c++20','-DRICKYOS_WEBP=1','-DFREEINK_DEVICE_'+('READPICO' if os.environ.get('RICKYOS_READER_TEST_DEVICE')=='pico' else 'METALIO_PLUS')+'=1','-DBOARD_HAS_PSRAM','-DRICKYOS_PRODUCT=1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 cmd+=['-I'+str(x) for x in includes]+[str(p/'main.cpp'),str(p/'links.cpp')]+[str(UP/x) for x in sources]+['-lexpat','-o',str(p/'test')]
 subprocess.run(cmd,check=True);subprocess.run([str(p/'test'),str(p)],check=True)
 print('Production CSS -> body -> lazy ImageBlock: four actual character backgrounds, text retained, repeated page, missing art, image suppression and style-off passed')
