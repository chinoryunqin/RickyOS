"""Run production bookmark and chapter-picker behavior with lightweight collaborators."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('RICKYOS_READER_SOURCE_ROOT', Path(__file__).resolve().parents[1] / 'firmware/rickyos-plus-1.2/upstream'))
SOURCE = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()


def method(signature):
    start = SOURCE.index(signature)
    cursor = SOURCE.index('{', start) + 1
    depth = 1
    while depth:
        depth += (SOURCE[cursor] == '{') - (SOURCE[cursor] == '}')
        cursor += 1
    return SOURCE[start:cursor]


class ReaderBindingActionsTest(unittest.TestCase):
    def test_bookmarks_and_chapter_results(self):
        code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
struct RenderLock {};
struct BookmarkEntry {
 float percentage = 0;
 std::string xpath, summary;
 int computedSpineIndex = 0, computedChapterPageCount = 0, computedChapterProgress = 0;
 uint32_t visibleTextOffset = 0;
 bool hasVisibleTextOffset = false;
};
struct SavedProgressPosition {float percentage = 0.25; std::string xpath = "chapter/line";};
struct ProgressRange {};
ProgressRange getPageProgressRange(const auto&, int, int, int) {return {};}
bool bookmarkMatchesProgress(const BookmarkEntry& b, int spine, int page, int, ProgressRange) {
 return b.computedSpineIndex == spine && b.computedChapterProgress == page;
}
struct ProgressMapper {static SavedProgressPosition toSavedProgress(const auto&, int) {return {};}};
struct BookmarkUtil {static std::string sanitizeBookmarkSummary(const std::string& s) {return s;}};
struct BookmarkFile {
 static inline int saves = 0;
 static bool save(const std::string&, const std::vector<BookmarkEntry>&) {++saves; return true;}
};
struct Section {
 int currentPage = 3, pageCount = 10;
 int estimatedTotalPages() {return pageCount;}
 std::string getTextFromSectionFile() {return "current page text";}
 std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t) {return 321;}
};
struct Epub {std::string getPath() {return "/books/test.epub";}};
struct ChapterResult {int spineIndex; std::string anchor;};
struct ActivityResult {bool isCancelled = false; std::variant<ChapterResult> data;};
struct EpubReaderChapterSelectionActivity {};
struct {int resumed = 0; void resumeSession() {++resumed;}} READING_STATS;
struct EpubReaderActivity {
 std::unique_ptr<Section> section = std::make_unique<Section>();
 std::shared_ptr<Epub> epub = std::make_shared<Epub>();
 std::vector<BookmarkEntry> cachedBookmarks;
 std::optional<uint32_t> currentPageVisibleOffset;
 bool bookmarkRemoved = false, currentPageBookmarked = false, failChild = false;
 int currentSpineIndex = 2, pendingManualTurn = 1, cachedSpineIndex = -1;
 int cachedChapterTotalPageCount = 0, nextPageNumber = -1, menus = 0, updates = 0, remembered = 0, cleared = 0;
 std::string pendingAnchor;
 std::function<void(const ActivityResult&)> callback;
 int getCurrentPosition() {return 0;}
 int currentTocIndex() {return 7;}
 void requestUpdate() {++updates;}
 void openReaderMenu() {++menus;}
 void rememberCurrentContentOffset() {++remembered;}
 void clearDeferredReposition() {++cleared;}
 template<class T, class F> bool startActivityForResultWith(F f, const auto&, int spine, int toc) {
  assert(spine == currentSpineIndex && toc == 7);
  assert(!section);
  if (failChild) return false;
  callback = f;
  return true;
 }
 void addBookmark(bool addOnly = false);
 void openChapterSelection(bool returnToMenu);
};
''' + method('void EpubReaderActivity::addBookmark(') + '\n' + method(
            'void EpubReaderActivity::openChapterSelection(') + r'''
int main() {
 EpubReaderActivity r;
 r.cachedBookmarks.reserve(4);
 r.addBookmark(true);
 assert(r.cachedBookmarks.size() == 1 && BookmarkFile::saves == 1);
 auto first = r.cachedBookmarks[0];
 assert(first.computedSpineIndex == 2 && first.computedChapterProgress == 3);
 assert(first.hasVisibleTextOffset && first.visibleTextOffset == 321 && first.summary == "current page text");
 // The new add action is idempotent, even when the UI flag is stale.
 r.currentPageBookmarked = false; r.bookmarkRemoved = true;
 r.addBookmark(true);
 assert(r.cachedBookmarks.size() == 1 && BookmarkFile::saves == 1);
 assert(r.currentPageBookmarked && !r.bookmarkRemoved);
 r.section->currentPage = 4; r.currentPageVisibleOffset = 456;
 r.addBookmark(true);
 assert(r.cachedBookmarks.size() == 2 && r.cachedBookmarks[0].visibleTextOffset == 456);
 // Existing menu/Home bookmark actions still toggle and preserve other pages.
 r.addBookmark();
 assert(r.cachedBookmarks.size() == 1 && r.bookmarkRemoved && !r.currentPageBookmarked);
 assert(r.cachedBookmarks[0].computedChapterProgress == 3);
 const auto saves = BookmarkFile::saves;
 r.section.reset(); r.addBookmark(true); assert(BookmarkFile::saves == saves);
 r.section = std::make_unique<Section>(); r.epub.reset();
 r.addBookmark(true); assert(BookmarkFile::saves == saves);
 // Both entry paths preserve the current chapter/page before freeing the section.
 for(bool returnToMenu : {false, true}) {
  EpubReaderActivity a; a.openChapterSelection(returnToMenu);
  assert(!a.section && a.cachedSpineIndex == 2 && a.nextPageNumber == 3);
  assert(a.cachedChapterTotalPageCount == 10 && a.pendingManualTurn == 0 && a.remembered == 1);
  a.callback(ActivityResult{true, ChapterResult{0,""}});
  assert(a.menus == (returnToMenu ? 1 : 0));
  EpubReaderActivity b; b.openChapterSelection(returnToMenu);
  b.callback(ActivityResult{false, ChapterResult{6,"target-anchor"}});
  assert(b.currentSpineIndex == 6 && b.pendingAnchor == "target-anchor" && b.nextPageNumber == 0);
  assert(!b.section && b.cleared == 1 && b.updates == 1 && b.menus == 0);
 }
 EpubReaderActivity oom; oom.failChild = true; oom.openChapterSelection(false);
 assert(!oom.section && !oom.callback && oom.updates == 1 && oom.nextPageNumber == 3);
}
'''
        with tempfile.TemporaryDirectory(prefix='reader-binding-actions-') as directory:
            p = Path(directory)
            (p / 'test.cpp').write_text(code)
            subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-variable', '-fsanitize=address,undefined',
                            str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
            subprocess.run([str(p / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
