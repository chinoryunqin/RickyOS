#include "RickyStorageActivity.h"
#ifdef RICKYOS_PRODUCT
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>

#include "FileBrowserActivity.h"
#include "SdCardFontSystem.h"
#include "activities/RenderLock.h"
#include "activities/settings/FontLibraryActivity.h"
#include "components/RickyPageUi.h"
#include "components/UITheme.h"
#include "components/icons/rickyPageIcons.h"
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

enum class Kind : uint8_t { Books, Images, AnyFile };

bool matches(const Kind kind, const char* name) {
  static constexpr const char* BOOK_EXTENSIONS[] = {".epub", ".txt",  ".md",   ".xtc", ".xtch",
                                                    ".pdf",  ".azw3", ".mobi", ".fb2"};
  switch (kind) {
    case Kind::Books:
      return std::any_of(std::begin(BOOK_EXTENSIONS), std::end(BOOK_EXTENSIONS), [name](const char* extension) {
        return FsHelpers::checkFileExtension(std::string_view(name), extension);
      });
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
  constexpr int kCap = 999;
  HalFile dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) return 0;
  int count = 0;
  char name[256];
  for (HalFile entry = dir.openNextFile(); entry && count < kCap; entry = dir.openNextFile()) {
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
  return std::min(count, kCap);
}
}  // namespace

const char* RickyStorageActivity::folderFor(const int index) {
  static constexpr const char* folders[] = {RickyStorageLayout::BOOKS, RickyStorageLayout::FONTS,
                                            RickyStorageLayout::IMAGES, RickyStorageLayout::DOWNLOADS};
  return index >= 0 && index < 4 ? folders[index] : "/";
}

// The free-space query walks the whole FAT and the counts walk folders: seconds on a
// full card. That ran in onEnter and froze the page, so it runs on its own task: the
// page opens at once with the last numbers and fills in fresh ones when they land.
// The last result outlives the page, so coming back shows numbers immediately.
namespace {
struct StorageScan {
  bool measured = false;
  uint64_t total = 0;
  uint64_t free = 0;
  std::array<int, 3> counts{-1, -1, -1};  // books, images, downloads
};
std::mutex scanMutex;
StorageScan lastScan;  // guarded by scanMutex
std::atomic<uint32_t> scansFinished{0};
std::atomic<bool> scanRunning{false};
std::atomic<bool> scanStale{true};
uint32_t lastScanMs = 0;
// A rescan on every visit would hold the card while the reader opens a book picked
// right after; this is long enough for tab hopping, short enough to stay current.
constexpr uint32_t kRescanAfterMs = 30 * 1000;

void scanStorageTask(void*) {
  StorageScan scan;
  scan.measured = Storage.getSpace(scan.total, scan.free);
  scan.counts[0] = countFiles(RickyStorageLayout::BOOKS, Kind::Books);
  scan.counts[1] = countFiles(RickyStorageLayout::IMAGES, Kind::Images);
  scan.counts[2] = countFiles(RickyStorageLayout::DOWNLOADS, Kind::AnyFile);
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    lastScan = scan;
    lastScanMs = millis();
  }
  scansFinished.fetch_add(1);
  scanRunning.store(false);
  vTaskDelete(nullptr);
}

void startStorageScan() {
  if (scanRunning.exchange(true)) return;
  scanStale.store(false);
  // countFiles recurses three folders deep with a 256-byte name buffer per level.
  if (xTaskCreate(&scanStorageTask, "RickyStorageScan", 6144, nullptr, 1, nullptr) != 1) {  // pdPASS
    LOG_ERR("STOR", "Cannot start the storage scan");
    scanRunning.store(false);
    scanStale.store(true);
  }
}
}  // namespace

void RickyStorageActivity::invalidateScan() { scanStale.store(true); }

void RickyStorageActivity::applyLastScan() {
  StorageScan scan;
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    scan = lastScan;
  }
  RenderLock lock(*this);
  seenScans = scansFinished.load();
  if (seenScans == 0) return;  // nothing measured yet this boot: keep the placeholders
  space = scan.measured ? Space::Ready : Space::Unavailable;
  sdTotalBytes = scan.total;
  sdFreeBytes = scan.free;
  counts[0] = scan.counts[0];
  counts[2] = scan.counts[1];
  counts[3] = scan.counts[2];
}

void RickyStorageActivity::onEnter() {
  UiListActivity::onEnter();
  // Downloaded fonts live in /.fonts and imported ones in /fonts: count the families
  // the font registry found in both, not the visible folder's entries. In memory, cheap.
  counts[1] = static_cast<int>(sdFontSystem.registry().getFamilies().size());
  applyLastScan();
  uint32_t age;
  {
    std::lock_guard<std::mutex> lock(scanMutex);
    age = millis() - lastScanMs;
  }
  if (scanStale.load() || scansFinished.load() == 0 || age > kRescanAfterMs) startStorageScan();
  requestUpdate();
}

void RickyStorageActivity::loop() {
  if (scansFinished.load() != seenScans) {
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
  auto device = title;
  device.x += title.width * 2 / 3;
  device.width = title.width / 3;
  auto right = small;
  right.align = fui::TextAlign::Right;
  target.text(device, tr(STR_SD_CARD), right);
  title.width -= device.width + gap;
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
  const int rowHeight = std::max(48, bodyHeight) + pad * 2;
  const int needed = summaryHeight + cellHeight * gridRows + rowHeight + gap * (gridRows + 1);
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
  const auto grid = screen.takeTop(cellHeight * gridRows + gap * (gridRows - 1), section);
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
    if (counts[i] >= 0)
      snprintf(text, sizeof(text), "%d", counts[i]);
    else
      snprintf(text, sizeof(text), "%s", space == Space::Measuring ? "…" : "—");
    target.text(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(top + bodyHeight), static_cast<int16_t>(width),
                          static_cast<int16_t>(smallHeight)},
                text, small);
  }

  // Transfer is an action, not a folder: a full-width card with a chevron.
  const auto transfer = screen.takeTop(rowHeight, 0);
  RickyPageUi::card(target, transfer, focus && selected == 5);
  screen.frame().hit(transfer, ACTION_ROW, 5, fui::InputTouch);
  const int middle = transfer.y + transfer.height / 2;
  RickyPageUi::pageIcon(target, renderer, transfer.x + pad, middle, icon_ricky_upload_40);
  target.text(fui::Rect{static_cast<int16_t>(transfer.x + pad + icon_ricky_upload_40.w + gap),
                        static_cast<int16_t>(middle - bodyHeight / 2),
                        static_cast<int16_t>(transfer.width - pad * 2 - icon_ricky_upload_40.w - gap - 24),
                        static_cast<int16_t>(bodyHeight)},
              tr(STR_RICKY_UPLOAD_FILES), label);
  RickyPageUi::chevron(target,
                       fui::Rect{static_cast<int16_t>(transfer.right() - 24 - pad), transfer.y, 24, transfer.height});
}

void RickyStorageActivity::activateIndex(int index) {
  app.clearTapFlash();
  folderMissing = false;
  if (index == 5) {
    activityManager.goToFileTransfer();
  } else if (index == 1) {
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
    // Files may have been deleted in there: count again on the way back.
    startActivityForResultWith<FileBrowserActivity>(
        [this](const ActivityResult&) {
          invalidateScan();
          startStorageScan();
        },
        path, FileBrowserActivity::Mode::Books, /*scoped=*/index != 4);
  }
}
#endif
