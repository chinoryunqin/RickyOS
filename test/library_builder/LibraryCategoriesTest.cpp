#include <HalStorage.h>
#include <gtest/gtest.h>

#include "LibraryCategories.h"

using namespace library;
namespace {
class LibraryCategoriesTest : public ::testing::Test {
 protected:
  LibraryCategories categories;
  void SetUp() override {
    fake::reset();
    ASSERT_EQ(categories.load(), LibraryCategories::Load::Missing);
  }
  void expectHashes(int slot, std::initializer_list<uint64_t> expected) {
    std::unique_ptr<uint64_t[]> hashes;
    size_t count;
    ASSERT_TRUE(categories.loadHashes(slot, hashes, count));
    ASSERT_EQ(count, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), hashes.get()));
  }
};
constexpr char path[] = "/.crosspoint/library-categories.v1";
}  // namespace
TEST_F(LibraryCategoriesTest, CreatesRenamesRejectsDuplicatesAndOverlongNames) {
  ASSERT_EQ(categories.create("小说"), 0);
  ASSERT_EQ(categories.create("工作"), 1);
  EXPECT_EQ(categories.create("小说"), -1);
  EXPECT_EQ(categories.create("    "), -1);
  EXPECT_EQ(categories.create("bad\nname"), -1);
  EXPECT_EQ(categories.create(std::string(49, 'x').c_str()), -1);
  EXPECT_FALSE(categories.rename(1, "小说"));
  ASSERT_TRUE(categories.rename(0, "阅读"));
  ASSERT_EQ(categories.load(), LibraryCategories::Load::Ready);
  EXPECT_STREQ(categories.name(0), "阅读");
  EXPECT_STREQ(categories.name(1), "工作");
}
TEST_F(LibraryCategoriesTest, RejectsInvalidUtf8AndAcceptsSixteenChineseCharacters) {
  EXPECT_EQ(categories.create("\xF0\x80\x80\x80"), -1);
  EXPECT_EQ(categories.create("\xED\xA0\x80"), -1);
  EXPECT_EQ(categories.create("\xE4\xB8"), -1);
  EXPECT_EQ(categories.create("\x80"), -1);
  EXPECT_EQ(categories.create("一二三四五六七八九十一二三四五六"), 0);
}
TEST_F(LibraryCategoriesTest, MultipleCategoriesAndBatchUpdatesPreserveUntouchedMembership) {
  ASSERT_EQ(categories.create("A"), 0);
  ASSERT_EQ(categories.create("B"), 1);
  const uint64_t first[] = {11, 33};
  ASSERT_TRUE(categories.assign(first, 2, 1, 1));
  const uint64_t second[] = {22, 33};
  ASSERT_TRUE(categories.assign(second, 2, 2, 2));
  expectHashes(0, {11, 33});
  expectHashes(1, {22, 33});
  const uint64_t all[] = {11, 22, 33};
  uint16_t any, common;
  ASSERT_TRUE(categories.selectionMasks(all, 3, any, common));
  EXPECT_EQ(any, 3);
  EXPECT_EQ(common, 0);
  ASSERT_TRUE(categories.assign(all, 3, 1, 1));
  expectHashes(0, {11, 22, 33});
  expectHashes(1, {22, 33});
  ASSERT_TRUE(categories.assign(second, 2, 1, 0));
  expectHashes(0, {11});
  expectHashes(1, {22, 33});
  expectHashes(-1, {11, 22, 33});
}
TEST_F(LibraryCategoriesTest, DeletingCategoryDoesNotDeleteBooksOrOtherCategoriesAndSlotReuseIsClean) {
  fake::add("/books/a.epub", "real content");
  ASSERT_EQ(categories.create("A"), 0);
  ASSERT_EQ(categories.create("B"), 1);
  const uint64_t ids[] = {42};
  ASSERT_TRUE(categories.assign(ids, 1, 3, 3));
  ASSERT_TRUE(categories.erase(0));
  EXPECT_TRUE(Storage.exists("/books/a.epub"));
  expectHashes(1, {42});
  ASSERT_EQ(categories.create("C"), 0);
  expectHashes(0, {});
  ASSERT_EQ(categories.load(), LibraryCategories::Load::Ready);
  expectHashes(1, {42});
  EXPECT_TRUE(fake::directoryEntriesByPath.empty());
}
TEST_F(LibraryCategoriesTest, BatchSummaryHandlesMixedMissingAndZeroPathFingerprint) {
  ASSERT_EQ(categories.create("A"), 0);
  const uint64_t known[] = {0, 8};
  ASSERT_TRUE(categories.assign(known, 2, 1, 1));
  uint16_t any, all;
  ASSERT_TRUE(categories.selectionMasks(known, 2, any, all));
  EXPECT_EQ(any, 1);
  EXPECT_EQ(all, 1);
  const uint64_t missing[] = {0, 1, 8, 99};
  ASSERT_TRUE(categories.selectionMasks(missing, 4, any, all));
  EXPECT_EQ(any, 1);
  EXPECT_EQ(all, 0);
}
TEST_F(LibraryCategoriesTest, LimitsAndBadBatchNeverDamagePriorData) {
  for (int i = 0; i < 16; ++i) ASSERT_EQ(categories.create(("C" + std::to_string(i)).c_str()), i);
  EXPECT_EQ(categories.create("overflow"), -1);
  const uint64_t duplicate[] = {1, 1}, unsorted[] = {9, 2};
  EXPECT_FALSE(categories.assign(duplicate, 2, 1, 1));
  EXPECT_FALSE(categories.assign(unsorted, 2, 1, 1));
  EXPECT_FALSE(categories.assign(nullptr, 0, 1, 1));
  ASSERT_EQ(categories.load(), LibraryCategories::Load::Ready);
  EXPECT_EQ(categories.activeMask(), 0xFFFF);
}
TEST_F(LibraryCategoriesTest, WriteAndRenameFailuresKeepPriorCatalog) {
  ASSERT_EQ(categories.create("A"), 0);
  const uint64_t ids[] = {11};
  ASSERT_TRUE(categories.assign(ids, 1, 1, 1));
  const auto original = fake::files[path]->bytes;
  fake::failWrite = 1;
  EXPECT_FALSE(categories.rename(0, "changed"));
  EXPECT_EQ(fake::files[path]->bytes, original);
  EXPECT_STREQ(categories.name(0), "A");
  fake::failRename = 1;  // fail installing temp after retaining old backup
  EXPECT_FALSE(categories.erase(0));
  ASSERT_EQ(categories.load(), LibraryCategories::Load::Ready);
  EXPECT_STREQ(categories.name(0), "A");
  expectHashes(0, {11});
}
TEST_F(LibraryCategoriesTest, DetectsTruncationCorruptionAndAllocationFailure) {
  ASSERT_EQ(categories.create("A"), 0);
  const uint64_t ids[] = {1, 2};
  ASSERT_TRUE(categories.assign(ids, 2, 1, 1));
  fake::failAlloc = 0;
  std::unique_ptr<uint64_t[]> hashes;
  size_t count;
  EXPECT_FALSE(categories.loadHashes(0, hashes, count));
  auto& data = fake::files[path]->bytes;
  data.back() ^= 1;
  EXPECT_EQ(categories.load(), LibraryCategories::Load::Error);
  data.pop_back();
  EXPECT_EQ(categories.load(), LibraryCategories::Load::Error);
}
TEST_F(LibraryCategoriesTest, RecoversInterruptedRenameFromBackup) {
  ASSERT_EQ(categories.create("A"), 0);
  ASSERT_TRUE(Storage.rename(path, "/.crosspoint/library-categories.bak"));
  ASSERT_EQ(categories.load(), LibraryCategories::Load::Ready);
  EXPECT_STREQ(categories.name(0), "A");
}
TEST_F(LibraryCategoriesTest, Full4096BookBatchHasBoundedSerializedSizeAndNoBookIo) {
  ASSERT_EQ(categories.create("A"), 0);
  uint64_t ids[4096];
  for (size_t i = 0; i < 4096; ++i) ids[i] = i;
  ASSERT_TRUE(categories.assign(ids, 4096, 1, 1));
  EXPECT_EQ(fake::files[path]->bytes.size(), 6 + 16 * 49 + 4096 * 10 + 4);
  ASSERT_EQ(categories.load(), LibraryCategories::Load::Ready);
  uint16_t any, all;
  ASSERT_TRUE(categories.selectionMasks(ids, 4096, any, all));
  EXPECT_EQ(any, 1);
  EXPECT_EQ(all, 1);
  EXPECT_TRUE(fake::directoryEntriesByPath.empty());
}
