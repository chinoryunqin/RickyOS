#include "RickyStorageActivity.h"
#ifdef RICKYOS_PRODUCT
#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

#include "CrossPointSettings.h"
#include "FileBrowserActivity.h"
#include "SdCardFontSystem.h"
#include "activities/RenderLock.h"
#include "activities/settings/FontLibraryActivity.h"
#include "components/RickyPageUi.h"
#include "components/UITheme.h"
#include "components/icons/rickyPageIcons.h"
#include "util/RickyFreeSpace.h"
#include "util/RickyStorageLayout.h"

void RickyStorageActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  drawPageHeader(Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, tr(STR_RICKY_STORAGE));
}
void RickyStorageActivity::drawFooter() {
  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_OPEN), true);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
namespace {
// "43.1 GB" / "820 MB": one decimal below 100 GB, whole numbers above.
void formatBytes(char* out, size_t size, uint64_t bytes) {
  constexpr uint64_t MB = 1024ULL * 1024ULL, GB = MB * 1024ULL;
  if (bytes >= 100 * GB)
    snprintf(out, size, "%u GB", static_cast<unsigned>((bytes + GB / 2) / GB));
  else if (bytes >= GB)
    snprintf(out, size, "%u.%u GB", static_cast<unsigned>(bytes / GB), static_cast<unsigned>(bytes % GB * 10 / GB));
  else
    snprintf(out, size, "%u MB", static_cast<unsigned>((bytes + MB / 2) / MB));
}

enum class Kind : uint8_t { Images, AnyFile };
// Counting stops here (shown as "999+"): past it the number says nothing a reader acts on.
constexpr int kCountCap = 999;

// Set when the page is left: a folder walk stops between entries and leaves the counts
// marked stale, so the next visit finishes them instead of the walk competing with a book.
std::atomic<bool> scanCancel{false};

bool matches(const Kind kind, const char* name) {
  switch (kind) {
    case Kind::Images:
      return FsHelpers::hasImageExtension(std::string_view(name));
    case Kind::AnyFile:
      return true;
  }
  return false;
}

// Files of `kind` inside `path` and its subfolders (the standby pictures sit in
// /images/待机图片), skipping hidden entries; capped so a huge card stays cheap.
int countFiles(const std::string& path, const Kind kind, const int depth = 0) {
  constexpr int kMaxDepth = 3;
  HalFile dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) return 0;
  int count = 0;
  char name[256];
  for (HalFile entry = dir.openNextFile(); entry && count < kCountCap && !scanCancel.load();
       entry = dir.openNextFile()) {
    name[0] = '\0';
    entry.getName(name, sizeof(name));
    const bool folder = entry.isDirectory();
    entry.close();
    if (name[0] == '.') continue;
    if (folder) {
      if (depth < kMaxDepth) count += countFiles(path + "/" + name, kind, depth + 1);
    } else if (matches(kind, name)) {
      ++count;
    }
  }
  dir.close();
  return std::min(count, kCountCap);
}
}  // namespace

const char* RickyStorageActivity::folderFor(const int index) {
  static constexpr const char* folders[] = {RickyStorageLayout::BOOKS, RickyStorageLayout::FONTS,
                                            RickyStorageLayout::IMAGES, RickyStorageLayout::DOWNLOADS};
  return index >= 0 && index < 4 ? folders[index] : "/";
}

// Numbers come from the cheapest source that is right, each shown as soon as it lands:
//  - total and free space: the volume (RickyFreeSpace, ~1 s on a 32 GB card, lock per slice);
//  - books: the Library index header, the very number Library shows; a folder walk only
//    while there is no fresh index;
//  - fonts: the font registry, in memory;
//  - images and downloads: a folder walk, lock per entry.
// The last numbers are saved on the card, so a visit after a restart shows them at once.
// Counts are redone only when something marked them stale (a transfer, a delete, ...; the
// mark is a file too, so it survives a restart) or on Refresh. Free space also drifts with
// reader caches, so a visit refreshes it once it is a few minutes old.
namespace {
struct StorageScan {
  bool measured = false;
  uint64_t total = 0;
  uint64_t free = 0;
  std::array<int, 3> counts{-1, -1, -1};  // books, images, downloads
};
constexpr const char* kStatsPath = "/.crosspoint/storage-stats.json";
// "1" = counts out of date. Rewritten, never deleted: this page removes nothing on the card.
constexpr const char* kStalePath = "/.crosspoint/storage-stats.stale";
constexpr uint32_t kFreeSpaceMaxAgeMs = 3 * 60 * 1000;

std::mutex scanMutex;
StorageScan lastScan;                     // guarded by scanMutex
bool haveScan = false;                    // guarded by scanMutex: lastScan holds real numbers
bool freeMeasured = false;                // guarded by scanMutex: measured since boot
uint32_t freeMeasuredMs = 0;              // guarded by scanMutex
std::atomic<uint32_t> scanGeneration{0};  // bumped on every result the page should show
std::atomic<bool> scanRunning{false};
std::atomic<bool> countsWanted{false};
std::atomic<bool> countsStale{false};  // mirrored by kStalePath
std::atomic<uint32_t> staleEpoch{0};   // invalidations; a count pass clears only its own epoch
bool loadedFromCard = false;           // loop task only

template <typename Update>
void publish(Update&& update) {
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    update(lastScan);
    haveScan = true;
  }
  scanGeneration.fetch_add(1);
}

void saveStats() {
  StorageScan scan;
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    scan = lastScan;
  }
  JsonDocument doc;
  doc["total"] = scan.total;
  doc["free"] = scan.free;
  doc["books"] = scan.counts[0];
  doc["images"] = scan.counts[1];
  doc["downloads"] = scan.counts[2];
  String text;
  serializeJson(doc, text);
  if (!Storage.writeFile(kStatsPath, text)) LOG_ERR("STOR", "Cannot save storage stats");
}

// Once per boot, on the first visit: a few hundred bytes, no walk.
void loadStats() {
  if (loadedFromCard) return;
  loadedFromCard = true;
  if (Storage.readFile(kStalePath) == "1") countsStale.store(true);
  const String text = Storage.readFile(kStatsPath);
  JsonDocument doc;
  if (text.isEmpty() || deserializeJson(doc, text) != DeserializationError::Ok) {
    countsStale.store(true);  // never counted on this card
    return;
  }
  publish([&](StorageScan& scan) {
    scan.total = doc["total"] | static_cast<uint64_t>(0);
    scan.free = doc["free"] | static_cast<uint64_t>(0);
    scan.measured = scan.total > 0 && scan.free <= scan.total;
    scan.counts = {doc["books"] | -1, doc["images"] | -1, doc["downloads"] | -1};
  });
}

// Books are whatever Library counts: its index header (a few ms). A folder walk of our
// own would apply other rules (formats Library does not open, reader records, duplicates)
// and show a number Library then contradicts. -1 while the index is missing or marked
// dirty (a transfer): the last number stays up until Library or Refresh rebuilds it.
// Loop task only: Library rebuilds the index there too, so the two never overlap.
int indexBookCount() {
  if (library::isLibraryIndexDirty()) return -1;
  auto index = makeUniqueNoThrow<library::LibraryIndexFile>();
  if (!index || !index->open(library::libraryIndexPath())) return -1;
  const int books = index->bookCount();
  index->close();
  return books;
}

void countPass() {
  const uint32_t epoch = staleEpoch.load();
  const uint32_t started = millis();
  struct Step {
    int slot;
    int (*count)();
  };
  // Books are not walked: their number is Library's (indexBookCount), read on the loop task.
  static constexpr Step steps[] = {
      {1, [] { return countFiles(RickyStorageLayout::IMAGES, Kind::Images); }},
      {2, [] { return countFiles(RickyStorageLayout::DOWNLOADS, Kind::AnyFile); }},
  };
  for (const Step& step : steps) {
    const int value = step.count();
    if (scanCancel.load()) {
      LOG_INF("STOR", "Counting paused after %u ms", static_cast<unsigned>(millis() - started));
      return;  // stays stale: the next visit counts again
    }
    publish([&](StorageScan& scan) { scan.counts[step.slot] = value; });
  }
  LOG_INF("STOR", "Counted files in %u ms", static_cast<unsigned>(millis() - started));
  if (staleEpoch.load() == epoch && countsStale.exchange(false)) Storage.writeFile(kStalePath, "0");
}

void scanStorageTask(void*) {
  uint64_t total = 0, free = 0;
  const bool measured = RickyFreeSpace::measure(total, free);
  publish([&](StorageScan& scan) {
    scan.measured = measured;
    scan.total = total;
    scan.free = free;
  });
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    freeMeasured = measured;
    freeMeasuredMs = millis();
  }
  while (!scanCancel.load() && countsWanted.exchange(false)) countPass();
  saveStats();
  scanRunning.store(false);
  scanGeneration.fetch_add(1);  // the page drops its "counting" state
  vTaskDelete(nullptr);
}

void startStorageScan(const bool counts) {
  if (counts) countsWanted.store(true);
  // Back on the page before a paused pass ended: let it carry on and pick countsWanted up.
  scanCancel.store(false);
  if (scanRunning.exchange(true)) return;
  // countFiles recurses three folders deep with a 256-byte name buffer per level.
  if (xTaskCreate(&scanStorageTask, "RickyStorageScan", 6144, nullptr, 1, nullptr) != 1) {  // pdPASS
    LOG_ERR("STOR", "Cannot start the storage scan");
    scanRunning.store(false);
  }
}
}  // namespace

void RickyStorageActivity::invalidateScan() {
  staleEpoch.fetch_add(1);
  if (!countsStale.exchange(true)) Storage.writeFile(kStalePath, "1");
}

void RickyStorageActivity::applyLastScan() {
  StorageScan scan;
  bool have;
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    scan = lastScan;
    have = haveScan;
  }
  RenderLock lock(*this);
  seenScans = scanGeneration.load();
  counting = scanRunning.load();
  if (!have) return;  // never measured on this card: keep the placeholders
  space = scan.measured ? Space::Ready : (counting ? Space::Measuring : Space::Unavailable);
  sdTotalBytes = scan.total;
  sdFreeBytes = scan.free;
  counts[0] = scan.counts[0];
  counts[2] = scan.counts[1];
  counts[3] = scan.counts[2];
}

void RickyStorageActivity::refresh() {
  // Refresh exists for a card changed elsewhere (a computer), which Library cannot see
  // either: rebuild Library's index first, exactly as Library's own rebuild does (~2 s on
  // a measured card), so the book count is the one Library will show.
  {
    RenderLock lock(*this);
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    if (!library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0)) {
      LOG_ERR("STOR", "Library index rebuild failed");
    }
  }
  const int indexed = indexBookCount();
  if (indexed >= 0) publish([&](StorageScan& scan) { scan.counts[0] = indexed; });
  invalidateScan();
  startStorageScan(true);
  applyLastScan();
  requestUpdate();
}

void RickyStorageActivity::onEnter() {
  UiListActivity::onEnter();
  // Downloaded fonts live in /.fonts and imported ones in /fonts: count the families
  // the font registry found in both, not the visible folder's entries. In memory, cheap.
  counts[1] = static_cast<int>(sdFontSystem.registry().getFamilies().size());
  loadStats();
  bool freeOld;
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    freeOld = !freeMeasured || millis() - freeMeasuredMs > kFreeSpaceMaxAgeMs;
  }
  // Library may have rebuilt its index since the last visit: take its count as it is now.
  const int indexed = indexBookCount();
  if (indexed >= 0) publish([&](StorageScan& scan) { scan.counts[0] = indexed; });
  const bool countsOld = countsStale.load();
  if (freeOld || countsOld) startStorageScan(countsOld);
  applyLastScan();
  requestUpdate();
}

void RickyStorageActivity::onExit() {
  scanCancel.store(true);  // a folder walk pauses; free space (~1 s) finishes on its own
  UiListActivity::onExit();
}

void RickyStorageActivity::loop() {
  if (scanGeneration.load() != seenScans) {
    applyLastScan();
    requestUpdate();
  }
  UiListActivity::loop();
}

void RickyStorageActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const auto content = pageContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(content.y), static_cast<int16_t>(content.x),
                  static_cast<int16_t>(renderer.getScreenHeight() - content.y - content.height),
                  static_cast<int16_t>(renderer.getScreenWidth() - content.x - content.width)});
  const auto& theme = screen.theme();
  auto& target = screen.target();
  screen.insetContent(fui::Insets{theme.spaceSm, theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int gap = std::max<int>(12, theme.spaceSm);
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  const int smallHeight = target.lineHeight(theme.smallText.font);
  RickyPageUi::Bold bold(renderer, 1);
  auto small = theme.smallText;
  small.maxLines = 1;
  auto label = theme.bodyText;
  label.maxLines = 1;
  const int selected = RickyPageUi::syncNav(nav, listCount());
  const bool focus = showMainTabContentSelection();

  auto title = screen.takeTop(target.lineHeight(theme.titleText.font), gap * 2);
  {
    // Refresh: count everything again (a card edited on a computer leaves no mark).
    const char* refreshLabel = counting ? tr(STR_RICKY_STORAGE_COUNTING) : tr(STR_RICKY_STORAGE_REFRESH);
    const int labelWidth = target.measureText(small.font, refreshLabel, small).width;
    const auto& icon = icon_ricky_refresh_40;
    const int iconGap = gap / 2;
    const int buttonWidth = icon.w + iconGap + labelWidth;
    const int buttonX = title.right() - buttonWidth;
    const int middle = title.y + title.height / 2;
    const fui::Rect hit{static_cast<int16_t>(buttonX - gap), static_cast<int16_t>(title.y - gap / 2),
                        static_cast<int16_t>(buttonWidth + gap * 2), static_cast<int16_t>(title.height + gap)};
    if (focus && selected == kRefreshAction) RickyPageUi::card(target, hit, true);
    RickyPageUi::pageIcon(target, renderer, buttonX, middle, icon);
    target.text(
        fui::Rect{static_cast<int16_t>(buttonX + icon.w + iconGap), static_cast<int16_t>(middle - smallHeight / 2),
                  static_cast<int16_t>(labelWidth + 2), static_cast<int16_t>(smallHeight)},
        refreshLabel, small);
    if (!counting) screen.frame().hit(hit, ACTION_ROW, kRefreshAction, fui::InputTouch);
    title.width = static_cast<int16_t>(buttonX - gap - title.x);
  }
  auto pageTitle = theme.titleText;
  pageTitle.bold = true;
  {
    RickyPageUi::Bold heading(renderer, 2);
    target.text(title, tr(STR_RICKY_STORAGE), pageTitle);
  }
  if (folderMissing) {
    auto warning = small;
    warning.maxLines = 2;
    target.text(screen.takeBottom(smallHeight * 2, gap), tr(STR_RICKY_FOLDER_MISSING), warning);
  }

  // Card sizes are fixed; leftover height becomes the gaps between them.
  const bool wide = content.width > content.height;
  const int columns = wide ? 4 : 2;
  const int gridRows = 4 / columns;
  const int pad = gap + gap / 2;
  const int ring = bodyHeight + smallHeight + gap * 3;
  const int summaryHeight = ring + pad * 2;
  const int cellHeight = std::max(48, bodyHeight + smallHeight) + pad * 2;
  const int needed = summaryHeight + cellHeight * gridRows + gap * gridRows;
  const int section = gap + std::clamp((screen.body().height - needed) / 4, 0, gap * 2);

  // SD card: usage ring, free / total, opens the whole card in the file browser.
  const auto summary = screen.takeTop(summaryHeight, section);
  RickyPageUi::card(target, summary, focus && selected == 4);
  screen.frame().hit(summary, ACTION_ROW, 4, fui::InputTouch);
  const Rect ringBox{summary.x + pad, summary.y + pad, ring, ring};
  const int percent =
      space == Space::Ready && sdTotalBytes ? static_cast<int>((sdTotalBytes - sdFreeBytes) * 100 / sdTotalBytes) : 0;
  RickyPageUi::usageRing(renderer, ringBox, percent, std::max(6, ring / 12));
  char text[48];
  snprintf(text, sizeof(text), "%d%%", percent);
  auto centered = small;
  centered.align = fui::TextAlign::Center;
  if (space == Space::Ready)
    target.text(fui::Rect{static_cast<int16_t>(ringBox.x), static_cast<int16_t>(ringBox.y + (ring - smallHeight) / 2),
                          static_cast<int16_t>(ring), static_cast<int16_t>(smallHeight)},
                text, centered);
  const int textX = ringBox.x + ring + pad;
  const int textWidth = summary.right() - textX - 24 - pad;
  const int textTop = summary.y + (summary.height - bodyHeight - smallHeight - gap / 2) / 2;
  auto strong = label;
  strong.bold = true;
  target.text(fui::Rect{static_cast<int16_t>(textX), static_cast<int16_t>(textTop), static_cast<int16_t>(textWidth),
                        static_cast<int16_t>(bodyHeight)},
              tr(STR_RICKY_STORAGE), strong);
  char freeText[16], totalText[16];
  formatBytes(freeText, sizeof(freeText), sdFreeBytes);
  formatBytes(totalText, sizeof(totalText), sdTotalBytes);
  snprintf(text, sizeof(text), tr(STR_RICKY_STORAGE_SPACE), freeText, totalText);
  target.text(fui::Rect{static_cast<int16_t>(textX), static_cast<int16_t>(textTop + bodyHeight + gap / 2),
                        static_cast<int16_t>(textWidth), static_cast<int16_t>(smallHeight)},
              space == Space::Ready       ? text
              : space == Space::Measuring ? tr(STR_LOADING)
                                          : tr(STR_NOT_AVAILABLE),
              small);
  RickyPageUi::chevron(target,
                       fui::Rect{static_cast<int16_t>(summary.right() - 24 - pad), summary.y, 24, summary.height});

  // Content folders as icon cards with a count, Boox-style.
  const StrId labels[] = {StrId::STR_RICKY_BOOK_FILES, StrId::STR_FONT, StrId::STR_RICKY_IMAGES,
                          StrId::STR_RICKY_DOWNLOADS};
  const freeink::Icon* icons[] = {&icon_ricky_books_40, &icon_ricky_fonts_40, &icon_ricky_images_40,
                                  &icon_ricky_downloads_40};
  const auto grid = screen.takeTop(cellHeight * gridRows + gap * (gridRows - 1), 0);
  for (int i = 0; i < 4; ++i) {
    const auto rect =
        RickyPageUi::uiRect(RickyPageLayout::cell(Rect{grid.x, grid.y, grid.width, grid.height}, i, 4, gap, columns));
    RickyPageUi::card(target, rect, focus && selected == i);
    screen.frame().hit(rect, ACTION_ROW, i, fui::InputTouch);
    const int middle = rect.y + rect.height / 2;
    RickyPageUi::pageIcon(target, renderer, rect.x + pad, middle, *icons[i]);
    const int x = rect.x + pad + icons[i]->w + gap;
    const int width = rect.right() - x - pad;
    const int top = middle - (bodyHeight + smallHeight) / 2;
    target.text(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(top), static_cast<int16_t>(width),
                          static_cast<int16_t>(bodyHeight)},
                I18N.get(labels[i]), label);
    if (counts[i] >= kCountCap)
      snprintf(text, sizeof(text), "%d+", kCountCap);
    else if (counts[i] >= 0)
      snprintf(text, sizeof(text), "%d", counts[i]);
    else
      snprintf(text, sizeof(text), "%s", counting || space == Space::Measuring ? "…" : "—");
    target.text(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(top + bodyHeight), static_cast<int16_t>(width),
                          static_cast<int16_t>(smallHeight)},
                text, small);
  }
}

void RickyStorageActivity::activateIndex(int index) {
  app.clearTapFlash();
  folderMissing = false;
  if (index == kRefreshAction) {
    refresh();
    return;
  }
  if (index == 1) {
    // Fonts are families managed in Font Management, most of them downloaded into
    // the hidden /.fonts: the visible /fonts folder would look empty.
    startActivityForResultWith<FontLibraryActivity>([this](const ActivityResult&) { requestUpdate(); });
  } else {
    // Every content card opens its fixed folder (created at boot; recreated here
    // if it was deleted since). The summary card opens the whole card.
    const char* path = folderFor(index);
    if (index < 4 && !Storage.exists(path) && !Storage.ensureDirectoryExists(path)) {
      folderMissing = true;
      requestUpdate();
      return;
    }
    // Scoped: Back in the opened folder returns here instead of climbing to "/".
    // A rename, move or delete in there marks the scan stale (FileBrowserActivity::finishEdit);
    // count again on the way back only then, not after every look into a folder.
    startActivityForResultWith<FileBrowserActivity>(
        [this](const ActivityResult&) {
          if (countsStale.load()) startStorageScan(true);
        },
        path, FileBrowserActivity::Mode::Books, /*scoped=*/index != 4);
  }
}
#endif
