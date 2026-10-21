#include <HalStorage.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"
#include "LibrarySources.h"

using namespace library;
namespace {
class LibrarySourcesTest : public ::testing::Test {
 protected:
  std::unique_ptr<LibrarySources> sources;
  BuildStats stats;
  void SetUp() override {
    fake::reset();
    sources = makeUniqueNoThrow<LibrarySources>();
  }
  bool build() {
    BuildOptions options;
    options.sources = sources.get();
    return buildLibraryIndex("/", stats, false, &options);
  }
};
}  // namespace
TEST_F(LibrarySourcesTest, SingleBookNeverEnumeratesItsParentOrOtherFolders) {
  fake::add("/books/one.md");
  fake::add("/books/two.txt");
  fake::add("/other/three.txt");
  ASSERT_TRUE(sources->add("/books/one.md", SourceKind::Book));
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 1);
  EXPECT_TRUE(fake::directoryEntriesByPath.empty());
}
TEST_F(LibrarySourcesTest, FolderOnlyAndRecursiveStayInsideSelectedRoot) {
  fake::add("/books/one.md");
  fake::add("/books/sub/two.txt");
  fake::add("/elsewhere/three.txt");
  ASSERT_TRUE(sources->add("/books", SourceKind::Folder));
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 1);
  EXPECT_EQ(fake::directoryEntriesByPath.count("/elsewhere/three.txt"), 0u);
  ASSERT_TRUE(sources->add("/books", SourceKind::RecursiveFolder));
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 2);
}
TEST_F(LibrarySourcesTest, OverlappingSourcesAndRepeatedBookDoNotDuplicate) {
  fake::add("/books/one.md");
  fake::add("/books/sub/two.txt");
  ASSERT_TRUE(sources->add("/books/one.md", SourceKind::Book));
  ASSERT_TRUE(sources->add("/books", SourceKind::RecursiveFolder));
  ASSERT_TRUE(sources->add("/books/sub", SourceKind::Folder));
  ASSERT_TRUE(sources->add("/books/one.md", SourceKind::Book));
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 2);
  EXPECT_EQ(sources->size(), 3);
}
TEST_F(LibrarySourcesTest, ExclusionSurvivesRefreshAndReAddingRestoresBook) {
  fake::add("/books/one.md");
  fake::add("/books/two.md");
  ASSERT_TRUE(sources->add("/books", SourceKind::Folder));
  ASSERT_TRUE(sources->exclude("/books/one.md"));
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 1);
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 1);
  EXPECT_TRUE(Storage.exists("/books/one.md"));
  ASSERT_TRUE(sources->add("/books/one.md", SourceKind::Book));
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 2);
}
TEST_F(LibrarySourcesTest, RemovingFolderKeepsManualBookAndDoesNotDeleteFiles) {
  fake::add("/books/one.md");
  fake::add("/books/two.md");
  ASSERT_TRUE(sources->add("/books", SourceKind::Folder));
  ASSERT_TRUE(sources->add("/books/one.md", SourceKind::Book));
  sources->erase(0);
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 1);
  EXPECT_TRUE(Storage.exists("/books/two.md"));
}
TEST_F(LibrarySourcesTest, OldCatalogMigratesAsReferencesWithoutEnrollingRoot) {
  fake::add("/old/a.txt");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  ASSERT_TRUE(LibrarySources::preserveLegacy());
  ASSERT_TRUE(sources->save());
  fake::add("/not-selected/new.txt");
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 1);
  ASSERT_TRUE(sources->exclude("/old/a.txt"));
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 0);
  EXPECT_TRUE(Storage.exists("/old/a.txt"));
}
TEST_F(LibrarySourcesTest, EmptyNewLibraryDoesNotScanRoot) {
  fake::add("/unselected/book.txt");
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 0);
  EXPECT_TRUE(fake::directoryEntriesByPath.empty());
}
TEST_F(LibrarySourcesTest, FirstOptInPreservesInterruptedCatalogBackup) {
  fake::add("/old.txt");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  ASSERT_TRUE(Storage.rename(libraryIndexPath(), "/.crosspoint/library.bak"));
  ASSERT_TRUE(LibrarySources::preserveLegacy());
  ASSERT_TRUE(sources->save());
  ASSERT_TRUE(build());
  EXPECT_EQ(stats.books, 1);
}
TEST_F(LibrarySourcesTest, IncrementalAddDoesNotRewalkEnrolledFolders) {
  fake::add("/books/old.txt");
  ASSERT_TRUE(sources->add("/books", SourceKind::Folder));
  ASSERT_TRUE(build());
  fake::add("/books/new.txt");
  fake::add("/books/do-not-import.txt");
  ASSERT_TRUE(sources->add("/books/new.txt", SourceKind::Book));
  fake::directoryEntriesByPath.clear();
  BuildOptions options;
  options.sources = sources.get();
  options.mergeExisting = true;
  options.sourceToScan = 1;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false, &options));
  EXPECT_EQ(stats.books, 2);
  EXPECT_TRUE(fake::directoryEntriesByPath.empty());
  ASSERT_TRUE(buildLibraryIndex("/", stats, false, &options));
  EXPECT_EQ(stats.books, 2);
}
TEST_F(LibrarySourcesTest, IncrementalExclusionDoesNotScanOrDiscoverOtherNewBooks) {
  fake::add("/books/one.txt");
  fake::add("/books/two.txt");
  ASSERT_TRUE(sources->add("/books", SourceKind::Folder));
  ASSERT_TRUE(build());
  fake::add("/books/not-imported.txt");
  ASSERT_TRUE(sources->exclude("/books/one.txt"));
  fake::directoryEntriesByPath.clear();
  BuildOptions options;
  options.sources = sources.get();
  options.mergeExisting = true;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false, &options));
  EXPECT_EQ(stats.books, 1);
  EXPECT_TRUE(fake::directoryEntriesByPath.empty());
  EXPECT_TRUE(Storage.exists("/books/one.txt"));
}
TEST_F(LibrarySourcesTest, IncrementalReAddingBookRefreshesItsStoredSize) {
  fake::add("/books/one.txt", "old");
  ASSERT_TRUE(sources->add("/books", SourceKind::Folder));
  ASSERT_TRUE(build());
  fake::add("/books/one.txt", "updated book contents");
  ASSERT_TRUE(sources->add("/books/one.txt", SourceKind::Book));
  BuildOptions options;
  options.sources = sources.get();
  options.mergeExisting = true;
  options.sourceToScan = 1;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false, &options));
  EXPECT_EQ(stats.books, 1);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(libraryIndexPath()));
  ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  EXPECT_EQ(record.fileSize, 21);
}
TEST_F(LibrarySourcesTest, SourceRemovalFiltersCatalogButPreservesLegacyAndManualReferences) {
  fake::add("/legacy/old.txt");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  ASSERT_TRUE(LibrarySources::preserveLegacy());
  fake::add("/books/one.txt");
  fake::add("/books/two.txt");
  ASSERT_TRUE(sources->add("/books", SourceKind::Folder));
  ASSERT_TRUE(sources->add("/books/one.txt", SourceKind::Book));
  ASSERT_TRUE(build());
  sources->erase(0);
  fake::directoryEntriesByPath.clear();
  BuildOptions options;
  options.sources = sources.get();
  options.mergeExisting = true;
  options.filterExistingSources = true;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false, &options));
  EXPECT_EQ(stats.books, 2);
  EXPECT_TRUE(fake::directoryEntriesByPath.empty());
  EXPECT_TRUE(Storage.exists("/books/two.txt"));
}
TEST_F(LibrarySourcesTest, CancelledImportLeavesOldIndexByteIdentical) {
  fake::add("/old.txt");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  const auto old = fake::files[libraryIndexPath()]->bytes;
  for (int i = 0; i < 100; ++i) fake::add("/books/" + std::to_string(i) + ".txt");
  ASSERT_TRUE(sources->add("/books", SourceKind::Folder));
  bool cancel = false;
  BuildOptions options;
  options.sources = sources.get();
  options.context = &cancel;
  options.progress = [](uint16_t count, void* ctx) {
    if (count >= 3) *static_cast<bool*>(ctx) = true;
  };
  options.shouldCancel = [](void* ctx) { return *static_cast<bool*>(ctx); };
  EXPECT_FALSE(buildLibraryIndex("/", stats, false, &options));
  EXPECT_TRUE(stats.cancelled);
  EXPECT_EQ(fake::files[libraryIndexPath()]->bytes, old);
}
TEST_F(LibrarySourcesTest, SourceManifestRoundTripsAndRejectsCorruption) {
  EXPECT_EQ(sources->load(), LibrarySources::Load::Missing);
  ASSERT_TRUE(sources->add("/books/中文.md", SourceKind::Book));
  ASSERT_TRUE(sources->add("/read", SourceKind::RecursiveFolder));
  ASSERT_TRUE(sources->save());
  ASSERT_EQ(sources->load(), LibrarySources::Load::Ready);
  EXPECT_EQ(sources->size(), 2);
  EXPECT_STREQ((*sources)[0].path, "/books/中文.md");
  fake::files["/.crosspoint/library-sources.v1"]->bytes.back() ^= 1;
  EXPECT_EQ(sources->load(), LibrarySources::Load::Error);
  EXPECT_EQ(sources->size(), 0);
}
TEST_F(LibrarySourcesTest, CancellingSourceEditRestoresPreviousManifest) {
  ASSERT_TRUE(sources->add("/old", SourceKind::Folder));
  ASSERT_TRUE(sources->save());
  ASSERT_TRUE(sources->add("/new", SourceKind::Folder));
  ASSERT_TRUE(sources->save(true));
  ASSERT_TRUE(LibrarySources::finishUpdate(false, true));
  ASSERT_EQ(sources->load(), LibrarySources::Load::Ready);
  EXPECT_EQ(sources->size(), 1);
  EXPECT_STREQ((*sources)[0].path, "/old");
}
TEST_F(LibrarySourcesTest, CancellingFirstOptInRestoresMissingManifestWithoutTouchingBooks) {
  fake::add("/book.md");
  ASSERT_TRUE(sources->add("/book.md", SourceKind::Book));
  ASSERT_TRUE(sources->save(true));
  ASSERT_TRUE(LibrarySources::finishUpdate(false, false));
  EXPECT_EQ(sources->load(), LibrarySources::Load::Missing);
  EXPECT_TRUE(Storage.exists("/book.md"));
}
TEST_F(LibrarySourcesTest, FailedManifestInstallRestoresPreviousRules) {
  ASSERT_TRUE(sources->add("/old", SourceKind::Folder));
  ASSERT_TRUE(sources->save());
  ASSERT_TRUE(sources->add("/new", SourceKind::Folder));
  fake::failRename = 1;
  EXPECT_FALSE(sources->save());
  ASSERT_EQ(sources->load(), LibrarySources::Load::Ready);
  EXPECT_EQ(sources->size(), 1);
  EXPECT_STREQ((*sources)[0].path, "/old");
}
TEST_F(LibrarySourcesTest, PathTraversalAndCapacityFailWithoutChangingExistingRules) {
  for (const char* bad : {"relative", "/a/../b", "/a/./b", "/a//b", "/a/", "/a\nb"})
    EXPECT_FALSE(sources->add(bad, SourceKind::Folder)) << bad;
  for (unsigned i = 0; i < LibrarySources::MAX_SOURCES; ++i)
    ASSERT_TRUE(sources->add(("/books/" + std::to_string(i) + ".md").c_str(), SourceKind::Book));
  EXPECT_FALSE(sources->add("/overflow.md", SourceKind::Book));
  EXPECT_EQ(sources->size(), LibrarySources::MAX_SOURCES);
}
TEST_F(LibrarySourcesTest, AllocationFailureKeepsPreviouslyAddedRules) {
  for (int i = 0; i < 8; ++i)
    ASSERT_TRUE(sources->add(("/books/" + std::to_string(i) + ".md").c_str(), SourceKind::Book));
  fake::failAlloc = 0;
  EXPECT_FALSE(sources->add("/books/ninth.md", SourceKind::Book));
  EXPECT_EQ(sources->size(), 8);
  EXPECT_STREQ((*sources)[0].path, "/books/0.md");
}
