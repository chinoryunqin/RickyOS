"""RickyOS fixed folders and the one-time move of legacy books into /books."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def run_cpp(program, extra_flags=()):
    with tempfile.TemporaryDirectory(prefix='free-space-') as directory:
        cpp = Path(directory) / 'check.cpp'
        exe = Path(directory) / 'check'
        cpp.write_text(program)
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', *extra_flags, str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


def body(source, signature):
    start = source.index(signature)
    depth = 0
    for index in range(source.index('{', start), len(source)):
        depth += {'{': 1, '}': -1}.get(source[index], 0)
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(signature)


class RickyStorageLayoutTest(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / 'src/util/RickyStorageLayout.cpp').read_text()
        self.header = (ROOT / 'src/util/RickyStorageLayout.h').read_text()

    def test_fixed_folders(self):
        folders = dict(re.findall(r'(BOOKS|FONTS|IMAGES|DOWNLOADS) = "([^"]+)"', self.header))
        self.assertEqual(folders, {'BOOKS': '/books', 'FONTS': '/fonts', 'IMAGES': '/images', 'DOWNLOADS': '/downloads'})
        # /fonts is where the SD font registry looks for visible families.
        self.assertIn('FONTS_DIR_VISIBLE = "/fonts"', (ROOT / 'lib/EpdFont/SdCardFontRegistry.h').read_text())

    def test_move_runs_once_never_overwrites_and_carries_reading_data(self):
        move = body(self.source, 'int moveBooksOnce(')
        self.assertIn('if (Storage.exists(marker)) return 0;', move)
        self.assertLess(move.index('Storage.exists(to.c_str())'), move.index('Storage.rename(from.c_str(), to.c_str())'))
        self.assertIn('relocateBookArtifacts(from, to)', move)
        self.assertIn('relocateBookReferences(from, to)', move)
        self.assertIn('if (isEmptyFolder(folder)) Storage.rmdir(folder);', move)
        self.assertIn('library::markLibraryIndexDirty()', move)
        self.assertNotIn('Storage.remove', self.source)
        self.assertIn('{"/book", "/Pushed Books"}', self.source)
        self.assertNotIn('WeRead', re.search(r'LEGACY_BOOK_FOLDERS\[\] = \{[^}]*\}', self.source).group(0))

    def test_weread_exports_into_books_and_old_exports_move_once(self):
        # WeRead finds its books by exact path, so the export folder and the move
        # target must agree; a separate marker reaches devices past the first move.
        store = (ROOT / 'lib/WeReadWebApi/src/WeReadStore.h').read_text()
        self.assertIn('constexpr const char* kExportDir = "/books";', store)
        path = body((ROOT / 'lib/WeReadWebApi/src/WeReadStore.cpp').read_text(), 'std::string finalBookPath(const BookRecord& book)')
        self.assertIn('std::string(kExportDir) + "/"', path)
        self.assertIn('Storage.ensureDirectoryExists(WeReadStore::kExportDir)',
                      (ROOT / 'lib/WeReadWebApi/src/WeReadClient.cpp').read_text())
        self.assertIn('WEREAD_BOOK_FOLDERS[] = {"/WeRead"}', self.source)
        self.assertIn('WEREAD_MIGRATION_MARKER[] = "/.crosspoint/ricky-storage-layout-2"', self.source)
        migrate = body(self.source, 'int migrateLegacyBooks()')
        # Folders other firmware owns (/book, /Pushed Books) stay put: the card may go back there.
        self.assertNotIn('moveBooksOnce(MIGRATION_MARKER, LEGACY_BOOK_FOLDERS)', migrate)
        self.assertIn('moveBooksOnce(WEREAD_MIGRATION_MARKER, WEREAD_BOOK_FOLDERS)', migrate)

    def test_boot_moves_after_recents_and_statistics_load(self):
        main = (ROOT / 'src/main.cpp').read_text()
        self.assertLess(main.index('RECENT_BOOKS.loadFromFile()'), main.index('RickyStorageLayout::migrateLegacyBooks()'))
        self.assertLess(main.index('READING_STATS.loadFromFile()'), main.index('RickyStorageLayout::migrateLegacyBooks()'))


    def test_storage_manual_breakdown_uses_index_sizes_and_bounded_folder_walks(self):
        page = (ROOT / 'src/activities/home/RickyStorageActivity.cpp').read_text()
        enter = body(page, 'void RickyStorageActivity::onEnter()')
        self.assertIn('sdFontSystem.registry().getFamilies().size()', enter)
        # The card walk runs on its own task so the page never freezes on a full card.
        self.assertNotIn('countFiles(', enter)
        self.assertNotIn('Storage.getSpace', enter)
        # Saved numbers first; free space when old, counts only when marked stale.
        self.assertIn('loadStats();', enter)
        self.assertNotIn('startStorageScan(', enter)
        self.assertIn('indexBookCount()', enter)
        scan = body(page, 'void scanStorageTask(void*)')
        # Sliced free-space walk: one long FAT scan used to hold the SD lock for seconds.
        self.assertIn('RickyFreeSpace::measure(total, free)', scan)
        # Free space is published before any folder walk starts.
        self.assertLess(scan.index('publish('), scan.index('countPass()'))
        self.assertIn('saveStats();', scan)
        count = body(page, 'void countPass()')
        self.assertIn('libraryBytes()', count)
        self.assertIn('sumBytes("/.crosspoint")', count)
        self.assertIn('sumBytes(RickyStorageLayout::FONTS) + sumBytes("/.fonts")', count)
        for field in ('scan.bookBytes = books', 'scan.cacheBytes = cache', 'scan.fontBytes = fonts'):
            self.assertIn(field, count)
        # A cancelled pass must not publish partial totals as a complete measurement.
        self.assertLess(count.index('if (scanCancel.load())'), count.index('publish('))
        self.assertIn('staleEpoch.load() == epoch', count)
        # Books are Library's number only: no walk of our own that Library would contradict.
        self.assertNotIn('Kind::Books)', page[page.index('void countPass()'):])
        self.assertNotIn('int countBooks()', page)
        self.assertIn('library::isLibraryIndexDirty()', body(page, 'int indexBookCount()'))
        self.assertIn('scanCancel.store(true);', body(page, 'void RickyStorageActivity::onExit()'))
        refresh = body(page, 'void RickyStorageActivity::refresh()')
        self.assertNotIn('buildLibraryIndex', refresh)
        self.assertIn('startStorageScan(true);', refresh)
        self.assertIn('Storage.writeFile(kStalePath, "1")', body(page, 'void RickyStorageActivity::invalidateScan()'))
        books = body(page, 'uint64_t libraryBytes()')
        self.assertIn('index->readRange(', books)
        self.assertIn('records[i].fileSize', books)
        self.assertNotIn('openNextFile(', books)
        walk = body(page, 'uint64_t sumBytes(')
        self.assertIn('constexpr int kMaxDepth = 4;', walk)
        self.assertIn('if (depth < kMaxDepth)', walk)
        self.assertIn('!scanCancel.load()', walk)
        actions = body(page, 'void RickyStorageActivity::activateIndex(')
        for activity in ('FileBrowserActivity', 'LibrarySourcesActivity'):
            self.assertIn('startActivityForResultWith<' + activity + '>', actions)
        self.assertIn('activityManager.goToFileTransfer()', actions)

    def test_storage_summary_cannot_open_cache_cleanup_or_take_button_focus(self):
        page = (ROOT / 'src/activities/home/RickyStorageActivity.cpp').read_text()
        header = (ROOT / 'src/activities/home/RickyStorageActivity.h').read_text()
        self.assertNotIn('ClearCacheActivity', page)
        self.assertNotIn('hit(summary,', page)
        self.assertIn('constexpr bool summarySelected = false;', page)
        self.assertIn('kCardCount = 3;', header)
        self.assertIn('const int row = i;', page)
        actions = body(page, 'void RickyStorageActivity::activateIndex(')
        self.assertRegex(actions, r'if \(index == 0\) \{\s*//[^\n]*\n\s*startActivityForResultWith<FileBrowserActivity>')
        self.assertIn('} else if (index == 1) {\n    startActivityForResultWith<LibrarySourcesActivity>', actions)
        self.assertIn('} else if (index == 2) {\n    activityManager.goToFileTransfer();', actions)
        # Only the summary loses its interaction: the existing statistics refresh remains.
        self.assertIn('if (index == kRefreshAction)', actions)
        self.assertIn('refresh();', actions)

    def test_free_space_walk_holds_the_sd_lock_one_slice_at_a_time(self):
        source = (ROOT / 'src/util/RickyFreeSpace.cpp').read_text()
        walk = body(source, 'bool measure(uint64_t& totalBytes, uint64_t& freeBytes) {\n  totalBytes = 0;')
        loop = walk[walk.index('for (uint32_t sector = 0;'):]
        # The lock is scoped to the read of one slice; counting and the yield run without it.
        locked = loop[loop.index('HalStorage::StorageLock lock;'):loop.index('freeClusters +=')]
        self.assertIn('readSectors(geometry.tableStart + sector, slice.get(), count)', locked)
        self.assertLess(loop.index('freeClusters +='), loop.index('taskYIELD();'))
        self.assertIn('rawBlockDevice() != geometry.device', locked)
        self.assertIn('constexpr uint32_t kSliceSectors = 16;', source)
        # Recount only after something changed: a folder visit alone does not.
        page = (ROOT / 'src/activities/home/RickyStorageActivity.cpp').read_text()
        self.assertIn('if (countsStale.load()) startStorageScan(true);', page)
        browser = (ROOT / 'src/activities/home/FileBrowserActivity.cpp').read_text()
        self.assertIn('RickyStorageActivity::invalidateScan();', body(browser, 'void FileBrowserActivity::finishEdit('))
        # The simulator has no FAT to walk.
        self.assertIn('#ifdef SIMULATOR', source)
        sd = (ROOT / 'freeink-sdk/libs/hardware/SDCardManager/include/SDCardManager.h').read_text()
        self.assertIn('FsVolume& mountedVolume() { return vol(); }', sd)

    def test_free_cluster_count_per_table_type(self):
        source = (ROOT / 'src/util/RickyFreeSpace.cpp').read_text()
        geometry = source[source.index('struct Geometry {'):source.index('};', source.index('struct Geometry {')) + 2]
        count = body(source, 'uint32_t countSlice(')
        program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
struct FsBlockDeviceInterface;
''' + geometry + '\n' + count + r'''
int main() {
  // FAT32: entries 0-1 reserved; clusters 2..6 = used, free, free, used(EOC), free; the
  // upper nibble is reserved and must not count as in use.
  {
    Geometry g; g.fatType = 32; g.clusters = 5;
    std::vector<uint8_t> t(512, 0xAA);
    const uint32_t entries[] = {0x0FFFFFF8, 0xFFFFFFFF, 3, 0xF0000000, 0, 0x0FFFFFFF, 0};
    for (int i = 0; i < 7; ++i) for (int b = 0; b < 4; ++b) t[i * 4 + b] = uint8_t(entries[i] >> (8 * b));
    assert(countSlice(g, t.data(), 0, 512) == 3);  // padding past cluster 6 is ignored
    // A later slice starts mid-table: the first 8 bytes are reserved entries only in slice 0.
    assert(countSlice(g, t.data() + 8, 8, 20) == 3);
  }
  // FAT16: two bytes per entry.
  {
    Geometry g; g.fatType = 16; g.clusters = 4;
    std::vector<uint8_t> t = {0xF8, 0xFF, 0xFF, 0xFF, 0, 0, 5, 0, 0, 0, 0xFF, 0xFF, 9, 9};
    assert(countSlice(g, t.data(), 0, uint32_t(t.size())) == 2);
  }
  // exFAT: one bit per cluster, set = used; bits past the last cluster are padding.
  {
    Geometry g; g.fatType = 64; g.clusters = 12;
    std::vector<uint8_t> t = {0x0F, 0xF1, 0xFF};
    assert(countSlice(g, t.data(), 0, 3) == 4 + 3);
    assert(countSlice(g, t.data() + 1, 1, 2) == 3);
  }
}
'''
        run_cpp(program)

    def test_production_book_byte_sum_has_bounded_stack_and_exact_batches(self):
        page = (ROOT / 'src/activities/home/RickyStorageActivity.cpp').read_text()
        method = body(page, 'uint64_t libraryBytes()')
        program = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>
''' + '#include "' + str(ROOT / 'lib/LibraryIndex/LibraryFormat.h') + '"\n' + r'''
std::atomic<bool> scanCancel{false};
bool allocationFails = false, openFails = false;
std::vector<library::ClixRecord> rows;
size_t readCount = 0, largestRead = 0, closes = 0;
namespace library {
const char* libraryIndexPath() { return "index"; }
class LibraryIndexFile {
  ClixHeader head{};
 public:
  bool open(const char*) { head.bookCount = rows.size(); return !openFails; }
  uint16_t bookCount() const { return head.bookCount; }
  const ClixHeader& header() const { return head; }
  bool readRange(uint32_t offset, void* destination, size_t bytes) {
    assert(bytes <= 1024 && bytes % sizeof(ClixRecord) == 0);
    assert(offset == readCount * 1024 && offset + bytes <= rows.size() * sizeof(ClixRecord));
    ++readCount; largestRead = std::max(largestRead, bytes);
    std::memcpy(destination, reinterpret_cast<const unsigned char*>(rows.data()) + offset, bytes);
    return true;
  }
  void close() { ++closes; }
};
}
template<class T> std::unique_ptr<T> makeUniqueNoThrow() {
  return allocationFails ? nullptr : std::unique_ptr<T>(new T);
}
''' + method + r'''
int main() {
  for (unsigned count : {0u, 1u, 7u, 8u, 9u, 31u, 32u, 33u, 3000u, 16384u}) {
    rows.assign(count, {}); readCount = largestRead = closes = 0;
    uint64_t expected = 0;
    for (unsigned i = 0; i < count; ++i) {
      rows[i].fileSize = 0xffff0000u + i;
      expected += rows[i].fileSize;
    }
    assert(libraryBytes() == expected);
    assert(readCount == (count + 7) / 8 && largestRead <= 1024 && closes == 1);
  }
  allocationFails = true; assert(libraryBytes() == 0); allocationFails = false;
  openFails = true; assert(libraryBytes() == 0); openFails = false;
  scanCancel = true; readCount = 0; assert(libraryBytes() == 0 && readCount == 0);
}
'''
        run_cpp(program, ('-Wframe-larger-than=2048', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'))


if __name__ == '__main__':
    unittest.main()

