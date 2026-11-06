"""Exercise actual folder enumeration and picker navigation, without device I/O."""
from pathlib import Path
import subprocess
import tempfile
import unittest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/tests"))
from test_reading_ui_regressions import method

PORT = Path(__file__).resolve().parents[2]
ROOT = PORT


def native(program):
    with tempfile.TemporaryDirectory(prefix='library-folder-picker-') as directory:
        work = Path(directory)
        (work / 'check.cpp').write_text(program)
        subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(work / 'check.cpp'), '-o', str(work / 'check')], check=True)
        subprocess.run([str(work / 'check')], check=True)


class LibraryFolderPickerTest(unittest.TestCase):
    def test_navigation_keeps_single_leaf_and_common_parent(self):
        source = (ROOT / 'src/activities/library/LibraryFoldersActivity.cpp').read_text()
        helpers = source[source.index('namespace {'):source.index('const char* LibraryFoldersActivity::headerTitle')]
        methods = '\n'.join(method(source, signature) for signature in (
            'void LibraryFoldersActivity::onEnter()',
            'void LibraryFoldersActivity::show(',
            'void LibraryFoldersActivity::activateIndex(',
            'void LibraryFoldersActivity::onBackButton()'))
        native(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
struct UiListActivity {void onEnter(){}};
struct RenderLock {template<class T>RenderLock(T&){}~RenderLock(){}};
constexpr int STR_LIBRARY_FOLDERS=1,STR_LIBRARY_ALL_FOLDERS=2,STR_LIBRARY_THIS_FOLDER=3;
const char* tr(int id){return id==1?"Folders":id==2?"All":"This";}
struct MappedInputManager {
 enum class Button {Confirm,Back};bool isPressed(Button)const{return false;}
} mappedInput;
struct FilePathResult {std::string path;};
struct ActivityResult {FilePathResult data;bool isCancelled=false;};
struct LibraryFoldersActivity:UiListActivity {
 struct Folder {std::string path;uint16_t count=0;};
 struct Row {std::string label,path,value;uint32_t count=0;bool opens=false;};
 std::vector<Folder> folders;std::vector<Row> rows;
 std::string selected,root,level,title;bool waitForButtons=false;
 struct {void clearTapFlash(){}} app;
 struct {int selected=0;void reset(int value){selected=value;}} nav;
 bool done=false,cancelled=false;std::string result;
 int listCount()const{return rows.size();}
 void onEnter();void show(const std::string&);void activateIndex(int);void onBackButton();
 void requestUpdate(){}void finish(){done=true;}
 void setResult(ActivityResult value){result=value.data.path;cancelled=value.isCancelled;}
};
''' + helpers + methods + r'''
int main(){
 LibraryFoldersActivity one;one.folders={{"/books/小说",5}};one.onEnter();
 assert(one.rows.size()==2);assert(one.root=="/books");
 assert(one.rows[1].path=="/books/小说");one.activateIndex(1);
 assert(one.done&&one.result=="/books/小说");
 LibraryFoldersActivity siblings;siblings.folders={{"/books/A",2},{"/books/B",3}};siblings.onEnter();
 assert(siblings.rows.size()==3);assert(siblings.rows[0].count==5);
 siblings.activateIndex(0);assert(siblings.done&&siblings.result.empty());
 LibraryFoldersActivity empty;empty.folders={{"/books/A",0},{"/books/B",0}};empty.onEnter();
 assert(empty.rows.size()==3);assert(empty.rows[1].count==0);assert(empty.rows[2].count==0);
 LibraryFoldersActivity nested;nested.folders={{"/books/A",0},{"/books/A/deep",4},{"/books/B",2}};
 nested.selected="/books/A/deep";nested.onEnter();
 assert(nested.root=="/books");assert(nested.level=="/books/A");
 assert(nested.rows[0].path=="/books/A");assert(nested.rows[1].path==nested.selected);
 nested.onBackButton();assert(nested.level=="/books");assert(!nested.done);
 nested.onBackButton();assert(nested.done&&nested.cancelled);
 LibraryFoldersActivity missing;missing.selected="/old/removed";missing.folders={{"/books/A",1}};
 missing.onEnter();assert(missing.rows.size()==2);
}
''')

    def test_enumeration_merges_enrolled_folders_without_duplicates(self):
        source = (ROOT / 'src/activities/library/LibraryListActivity.cpp').read_text()
        production = method(source, 'void LibraryListActivity::openFolders()')
        native(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
struct RenderLock {template<class T>RenderLock(T&){}~RenderLock(){}};
namespace library {
 enum class SourceKind {Folder,RecursiveFolder,Book,ExcludedBook};
 struct LibrarySource {SourceKind kind;std::string path;};
 struct LibrarySources {
  enum class Load {Missing,Ready,Error};
  inline static std::vector<LibrarySource> rules;
  inline static Load status=Load::Ready;
  Load load(){return status;}size_t size()const{return status==Load::Ready?rules.size():0;}
  const LibrarySource& operator[](size_t i)const{return rules[i];}
 };
}
struct LibraryFoldersActivity {struct Folder {std::string path;uint16_t count=0;};};
struct FilePathResult {std::string path;};
struct ActivityResult {bool isCancelled=false;std::variant<FilePathResult,int> data;};
struct {int saves=0;bool rememberLibraryFolder(const std::string&){++saves;return true;}} APP_STATE;
struct LibraryListActivity {
 struct Index {
  bool opened=true,readOkay=true;std::vector<std::string> paths;std::vector<uint16_t> counts;
  bool isOpen()const{return opened;}
  bool readFolderPaths(std::vector<std::string>& out){out=paths;return readOkay;}
  bool countBooksPerFolder(std::vector<uint16_t>& out){out=counts;return readOkay;}
  int bookCount()const{return 0;}
 } index;
 struct {void clearTapFlash(){}} app;
 struct Nav {void reset(){}} nav;
 std::string folderPath;std::vector<LibraryFoldersActivity::Folder> offered;
 std::function<void(const ActivityResult&)> callback;
 Nav& activeNav(){return nav;}void applyFilter(){}void swallowHeldReleases(){}void requestUpdate(){}
 template<class T,class F>void startActivityForResultWith(F fn,std::vector<LibraryFoldersActivity::Folder> folders,
     const std::string&){offered=std::move(folders);callback=fn;}
 void openFolders();
};
''' + production + r'''
int main(){
 using Kind=library::SourceKind;using Sources=library::LibrarySources;
 Sources::rules={{Kind::Folder,"/books/A"},{Kind::RecursiveFolder,"/books/B"},
                 {Kind::Book,"/loose/book.epub"},{Kind::ExcludedBook,"/books/A/no.epub"}};
 LibraryListActivity empty;empty.index.paths={};empty.index.counts={};empty.openFolders();
 assert(empty.offered.size()==2);assert(empty.offered[0].path=="/books/A");
 assert(empty.offered[1].path=="/books/B");
 LibraryListActivity mixed;mixed.index.paths={"/books/A","/books/B/deep"};mixed.index.counts={3,7};
 mixed.openFolders();assert(mixed.offered.size()==3);
 int countA=0;for(const auto& folder:mixed.offered)if(folder.path=="/books/A"){++countA;assert(folder.count==3);}
 assert(countA==1);
 ActivityResult cancel;cancel.isCancelled=true;cancel.data=FilePathResult{"/books/B"};
 mixed.callback(cancel);assert(mixed.folderPath.empty()&&APP_STATE.saves==0);
 cancel.isCancelled=false;mixed.callback(cancel);assert(mixed.folderPath=="/books/B"&&APP_STATE.saves==1);
 LibraryListActivity unavailable;unavailable.index.opened=false;unavailable.openFolders();
 assert(unavailable.offered.size()==2);
 Sources::status=Sources::Load::Error;
 LibraryListActivity badSources;badSources.index.paths={"/books/A"};badSources.index.counts={2};badSources.openFolders();
 assert(badSources.offered.size()==1);
}
''')


if __name__ == '__main__':
    unittest.main()
