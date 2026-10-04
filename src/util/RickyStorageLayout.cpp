#include "RickyStorageLayout.h"
#ifdef RICKYOS_PRODUCT
#include <FsHelpers.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>

#include <string>
#include <vector>

#include "BookCacheUtils.h"

namespace RickyStorageLayout {
namespace {
// Written once the move has run, so a /book folder created later is left alone.
constexpr char MIGRATION_MARKER[] = "/.crosspoint/ricky-storage-layout-1";
constexpr const char* LEGACY_BOOK_FOLDERS[] = {"/book", "/Pushed Books"};
constexpr const char* BOOK_EXTENSIONS[] = {".epub", ".txt", ".md", ".xtc", ".xtch", ".pdf", ".azw3", ".mobi", ".fb2"};

bool isBookFile(const std::string& name) {
  for (const char* extension : BOOK_EXTENSIONS) {
    if (FsHelpers::checkFileExtension(name, extension)) return true;
  }
  return false;
}

// Plain book files directly inside `folder`; listed first, moved afterwards, so
// renames never disturb the directory iteration.
std::vector<std::string> bookFilesIn(const char* folder) {
  std::vector<std::string> names;
  HalFile dir = Storage.open(folder);
  if (!dir || !dir.isDirectory()) return names;
  char name[256];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    name[0] = '\0';
    entry.getName(name, sizeof(name));
    if (!entry.isDirectory() && name[0] != '.' && isBookFile(name)) names.emplace_back(name);
    entry.close();
  }
  dir.close();
  return names;
}

bool isEmptyFolder(const char* folder) {
  HalFile dir = Storage.open(folder);
  if (!dir || !dir.isDirectory()) return false;
  HalFile entry = dir.openNextFile();
  const bool empty = !entry;
  if (entry) entry.close();
  dir.close();
  return empty;
}
}  // namespace

void ensureFolders() {
  for (const char* folder : {BOOKS, FONTS, IMAGES, DOWNLOADS}) {
    if (!Storage.exists(folder) && !Storage.ensureDirectoryExists(folder))
      LOG_ERR("LAYOUT", "cannot create %s", folder);
  }
}

int migrateLegacyBooks() {
  if (Storage.exists(MIGRATION_MARKER)) return 0;
  ensureFolders();
  int moved = 0, skipped = 0;
  for (const char* folder : LEGACY_BOOK_FOLDERS) {
    if (!Storage.exists(folder)) continue;
    for (const std::string& name : bookFilesIn(folder)) {
      const std::string from = std::string(folder) + "/" + name;
      const std::string to = std::string(BOOKS) + "/" + name;
      if (Storage.exists(to.c_str()) || !Storage.rename(from.c_str(), to.c_str())) {
        LOG_INF("LAYOUT", "left in place: %s", from.c_str());
        ++skipped;
        continue;
      }
      // Same bookkeeping as the file browser's Move: caches, bookmarks, recents,
      // statistics and the open-book record follow the file.
      if (!relocateBookArtifacts(from, to) || !relocateBookReferences(from, to))
        LOG_ERR("LAYOUT", "moved %s but some reading data did not follow", from.c_str());
      ++moved;
    }
    // Only an emptied folder is removed; anything else in it stays.
    if (isEmptyFolder(folder)) Storage.rmdir(folder);
  }
  if (moved > 0) library::markLibraryIndexDirty();
  HalFile marker = Storage.open(MIGRATION_MARKER, O_WRITE | O_CREAT | O_TRUNC);
  if (marker) marker.close();
  LOG_INF("LAYOUT", "legacy books: %d moved to %s, %d left in place", moved, BOOKS, skipped);
  return moved;
}
}  // namespace RickyStorageLayout
#endif
