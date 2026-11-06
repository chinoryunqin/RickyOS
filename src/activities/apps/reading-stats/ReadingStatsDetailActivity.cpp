#include "ReadingStatsDetailActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "BookReadingAdjustmentActivity.h"
#include "InxItemLayout.h"
#include "ReadingStatsStore.h"
#include "components/RickyDesignUi.h"
#include "components/RickyStatsLayout.h"
#include "components/UITheme.h"
#include "components/icons/settings2.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int COVER_WIDTH = 96;
constexpr int COVER_HEIGHT = 140;
constexpr int DETAIL_FOCUS_ITEM_COUNT = 2;
constexpr int DETAIL_ADJUST_FOCUS_INDEX = 1;
constexpr int ADJUST_BUTTON_SIZE = ricky::ds::layout::kTouchMin;
constexpr int SUMMARY_BANNER_GAP = 8;
constexpr int DETAIL_SCROLL_STEP = 128;
constexpr size_t MAX_RESOLVED_COVERS = 16;
constexpr uint64_t MIN_ESTIMATE_READING_MS = 10ULL * 60ULL * 1000ULL;
constexpr uint8_t MIN_ESTIMATE_PROGRESS_PERCENT = 5;
constexpr uint64_t MIN_ESTIMATE_AVG_SESSION_MS = 5ULL * 60ULL * 1000ULL;
constexpr uint64_t ESTIMATE_ROUNDING_MS = 5ULL * 60ULL * 1000ULL;

struct ResolvedCoverCacheEntry {
  std::string bookPath;
  std::string coverBmpPath;
  std::string resolvedPath;
};

std::vector<ResolvedCoverCacheEntry>& getResolvedCoverCache() {
  static std::vector<ResolvedCoverCacheEntry> cache;
  return cache;
}

std::string getCachedResolvedCoverPath(const ReadingBookStats& book) {
  auto& cache = getResolvedCoverCache();
  for (auto it = cache.begin(); it != cache.end(); ++it) {
    if (it->bookPath != book.path || it->coverBmpPath != book.coverBmpPath) {
      continue;
    }
    if (!it->resolvedPath.empty() && Storage.exists(it->resolvedPath.c_str())) {
      if (it != cache.begin()) {
        ResolvedCoverCacheEntry entry = *it;
        cache.erase(it);
        cache.insert(cache.begin(), std::move(entry));
      }
      return cache.front().resolvedPath;
    }
    break;
  }
  return "";
}

void rememberResolvedCoverPath(const ReadingBookStats& book, const std::string& resolvedPath) {
  if (resolvedPath.empty()) {
    return;
  }

  auto& cache = getResolvedCoverCache();
  cache.erase(std::remove_if(cache.begin(), cache.end(),
                             [&](const ResolvedCoverCacheEntry& entry) { return entry.bookPath == book.path; }),
              cache.end());
  cache.insert(cache.begin(), ResolvedCoverCacheEntry{book.path, book.coverBmpPath, resolvedPath});
  if (cache.size() > MAX_RESOLVED_COVERS) {
    cache.pop_back();
  }
}

ReadingBookStats withCoverPath(const ReadingBookStats& book, const std::string& coverBmpPath) {
  ReadingBookStats updated = book;
  updated.coverBmpPath = coverBmpPath;
  return updated;
}

const ReadingBookStats* findBook(const std::string& bookPath) {
  for (const auto& book : READING_STATS.getBooks()) {
    // cppcheck-suppress useStlAlgorithm
    if (book.path == bookPath) {
      return &book;
    }
  }
  return nullptr;
}

std::string resolveStoredCoverPath(const std::string& coverBmpPath) {
  if (coverBmpPath.empty()) {
    return "";
  }

  if (coverBmpPath.find("[HEIGHT]") != std::string::npos) {
    const int candidateHeights[] = {COVER_HEIGHT, 160, 240, 400};
    for (const int height : candidateHeights) {
      const std::string resolved = UITheme::getCoverThumbPath(coverBmpPath, height);
      if (Storage.exists(resolved.c_str())) {
        return resolved;
      }
    }
    return "";
  }

  return Storage.exists(coverBmpPath.c_str()) ? coverBmpPath : "";
}

std::string ensureCoverPath(const ReadingBookStats& book) {
  const std::string cachedResolvedPath = getCachedResolvedCoverPath(book);
  if (!cachedResolvedPath.empty()) {
    return cachedResolvedPath;
  }

  std::string resolved = resolveStoredCoverPath(book.coverBmpPath);
  if (!resolved.empty()) {
    rememberResolvedCoverPath(book, resolved);
    return resolved;
  }

  std::string title;
  std::string author;
  const std::string coverPath = BookCoverLoader::ensureFullCover(book.path, &title, &author);
  if (coverPath.empty()) return "";

  READING_STATS.updateBookMetadata(book.path, title, author, coverPath);
  rememberResolvedCoverPath(withCoverPath(book, coverPath), coverPath);
  return coverPath;
}

std::string findFastCoverPath(const ReadingBookStats& book) {
  const std::string cachedResolvedPath = getCachedResolvedCoverPath(book);
  if (!cachedResolvedPath.empty()) {
    return cachedResolvedPath;
  }

  std::string resolved = resolveStoredCoverPath(book.coverBmpPath);
  if (!resolved.empty()) {
    rememberResolvedCoverPath(book, resolved);
    return resolved;
  }

  if (!Storage.exists(book.path.c_str())) {
    return "";
  }

  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(epub.getCoverBmpPath());
  } else if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    Txt txt(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(txt.getCoverBmpPath());
  }

  if (!resolved.empty()) {
    READING_STATS.updateBookMetadata(book.path, "", "", resolved);
    rememberResolvedCoverPath(withCoverPath(book, resolved), resolved);
  }
  return resolved;
}

std::string getDisplayTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

std::string formatDate(const uint32_t timestamp) {
  const std::string formatted = TimeUtils::formatDate(timestamp);
  return formatted.empty() ? std::string(tr(STR_NOT_SET)) : formatted;
}

std::string formatDateRange(const uint32_t startTimestamp, const uint32_t endTimestamp) {
  const std::string start = TimeUtils::formatDate(startTimestamp);
  const std::string end = TimeUtils::formatDate(endTimestamp);
  return (start.empty() ? "?" : start) + " - " + (end.empty() ? "?" : end);
}

uint32_t getCompletionDateForDisplay(const ReadingBookStats& book) { return book.completedAt; }

uint64_t roundUpEstimateMs(const uint64_t valueMs) {
  if (valueMs == 0) {
    return 0;
  }
  return ((valueMs + ESTIMATE_ROUNDING_MS - 1) / ESTIMATE_ROUNDING_MS) * ESTIMATE_ROUNDING_MS;
}

std::string buildSessionEstimateText(const uint64_t remainingMs, const ReadingBookStats& book) {
  if (book.sessions == 0) {
    return "";
  }

  const uint64_t averageSessionMs = book.totalReadingMs / book.sessions;
  if (averageSessionMs < MIN_ESTIMATE_AVG_SESSION_MS) {
    return "";
  }

  const uint32_t sessionsLeft = static_cast<uint32_t>((remainingMs + averageSessionMs - 1) / averageSessionMs);
  if (sessionsLeft == 0) {
    return "";
  }

  return std::to_string(sessionsLeft) + " " +
         (sessionsLeft == 1 ? std::string(tr(STR_SESSION)) : std::string(tr(STR_SESSIONS)));
}

std::string buildEstimatedTimeLeftText(const ReadingBookStats& book) {
  if (book.completed || book.lastProgressPercent >= 100) {
    return tr(STR_DONE);
  }

  if (book.totalReadingMs < MIN_ESTIMATE_READING_MS || book.lastProgressPercent < MIN_ESTIMATE_PROGRESS_PERCENT) {
    return tr(STR_ESTIMATE_AFTER_MORE_READING);
  }

  const uint64_t estimatedTotalMs =
      (book.totalReadingMs * 100ULL + book.lastProgressPercent - 1) / book.lastProgressPercent;
  if (estimatedTotalMs <= book.totalReadingMs) {
    return tr(STR_ESTIMATE_AFTER_MORE_READING);
  }

  const uint64_t remainingMs = roundUpEstimateMs(estimatedTotalMs - book.totalReadingMs);
  std::string estimateText = "~" + ReadingStatsAnalytics::formatDurationHm(remainingMs);
  const std::string sessionText = buildSessionEstimateText(remainingMs, book);
  if (!sessionText.empty()) {
    estimateText += " / " + sessionText;
  }
  return estimateText;
}

void drawMetricCard(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  namespace design = RickyDesignUi;
  namespace ds = ricky::ds;
  const freeink::ui::Rect card{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y),
                               static_cast<int16_t>(rect.width), static_cast<int16_t>(rect.height)};
  design::softCard(renderer, card, false);
  const int pad = ds::space::kCardPadding;
  const int valueHeight = std::max(renderer.getLineHeight(design::HEADLINE), renderer.getLineHeight(design::BODY));
  const freeink::ui::Rect valueBox{static_cast<int16_t>(rect.x + pad), static_cast<int16_t>(rect.y + pad),
                                   static_cast<int16_t>(rect.width - pad * 2), static_cast<int16_t>(valueHeight)};
  design::text(renderer, valueBox, value.c_str(), design::HEADLINE);
  design::secondaryText(renderer,
                        {valueBox.x, static_cast<int16_t>(valueBox.bottom() + ds::space::kTextStackGap), valueBox.width,
                         static_cast<int16_t>(rect.height - pad * 2 - valueHeight - ds::space::kTextStackGap)},
                        label, design::CAPTION, false, freeink::ui::TextAlign::Left, 2);
}

void drawAdjustTimeButton(const GfxRenderer& renderer, const Rect& rect, const bool selected) {
  RickyDesignUi::fill(renderer,
                      {static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                       static_cast<int16_t>(rect.height)},
                      selected ? ricky::ds::gray::kInk : ricky::ds::gray::kPaper, ricky::ds::radius::kControl);
  const bool foregroundBlack = !selected;

  constexpr int iconSize = 32;  // Settings2Icon is a native 32 x 32 mask.
  const int iconX = rect.x + (rect.width - iconSize) / 2;
  const int iconY = rect.y + (rect.height - iconSize) / 2;
  if (foregroundBlack)
    renderer.drawIcon(Settings2Icon, iconX, iconY, iconSize);
  else
    renderer.drawIconInverted(Settings2Icon, iconX, iconY, iconSize);
}

Rect offsetRect(Rect rect, const int dy) {
  rect.y += dy;
  return rect;
}

struct DetailHeroLayout {
  Rect coverBase;
  Rect adjustBase;
  Rect cover;
  Rect adjust;
};

DetailHeroLayout detailHeroLayout(const ThemeMetrics& metrics, const int contentTop, const int coverWidth,
                                  const int coverHeight, const int scrollOffset) {
  const Rect coverBase{metrics.contentSidePadding, contentTop, coverWidth, coverHeight};
  const Rect adjustBase{coverBase.x + (coverBase.width - ADJUST_BUTTON_SIZE) / 2,
                        coverBase.y + coverBase.height + metrics.verticalSpacing, ADJUST_BUTTON_SIZE,
                        ADJUST_BUTTON_SIZE};
  return {coverBase, adjustBase, offsetRect(coverBase, -scrollOffset), offsetRect(adjustBase, -scrollOffset)};
}

void drawSummaryBanner(const GfxRenderer& renderer, const Rect& rect, const char* title, const std::string& summary,
                       const bool inverted = false) {
  namespace design = RickyDesignUi;
  namespace ds = ricky::ds;
  design::softCard(renderer,
                   {static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                    static_cast<int16_t>(rect.height)},
                   inverted);
  const int pad = ds::space::kCardPadding;
  const int line = std::max(renderer.getLineHeight(design::CAPTION), renderer.getLineHeight(design::BODY));
  design::text(renderer,
               {static_cast<int16_t>(rect.x + pad), static_cast<int16_t>(rect.y + pad),
                static_cast<int16_t>(rect.width - pad * 2), static_cast<int16_t>(line)},
               title, design::CAPTION, inverted);
  design::secondaryText(renderer,
                        {static_cast<int16_t>(rect.x + pad), static_cast<int16_t>(rect.y + pad + line + 4),
                         static_cast<int16_t>(rect.width - pad * 2), static_cast<int16_t>(line * 2)},
                        summary.c_str(), design::CAPTION, inverted, freeink::ui::TextAlign::Left, 2);
}

void drawProgressBlock(const GfxRenderer& renderer, const Rect& rect, const char* label, const uint8_t percent) {
  namespace design = RickyDesignUi;
  namespace ds = ricky::ds;
  char text[8];
  snprintf(text, sizeof(text), "%u%%", std::min<unsigned>(percent, 100));
  const int line = std::max(renderer.getLineHeight(design::CAPTION), renderer.getLineHeight(design::BODY));
  const int valueWidth = renderer.getTextWidth(design::CAPTION, text);
  design::secondaryText(
      renderer,
      {static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y),
       static_cast<int16_t>(std::max(1, rect.width - valueWidth - ds::space::kInlineGap)), static_cast<int16_t>(line)},
      label);
  design::text(renderer,
               {static_cast<int16_t>(rect.x + rect.width - valueWidth), static_cast<int16_t>(rect.y),
                static_cast<int16_t>(valueWidth), static_cast<int16_t>(line)},
               text, design::CAPTION);
  design::progress(renderer,
                   {static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y + line + ds::space::k2),
                    static_cast<int16_t>(rect.width), ds::layout::kProgressHeight},
                   percent);
}

void drawCover(const GfxRenderer& renderer, const Rect& rect, const std::string& coverPath) {
  const auto drawFallback = [&renderer, &rect]() {
    const char* label = tr(STR_BOOK);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int textY = rect.y + rect.height / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
  };

  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  if (coverPath.empty()) {
    drawFallback();
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("RSD", coverPath, file)) {
    drawFallback();
    return;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    if (UITheme::getInstance().hasMainTabs()) {
      if (!renderer.drawBitmapCropToFill(bitmap, rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4)) {
        drawFallback();
      }
    } else {
      renderer.drawBitmap(bitmap, rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4);
    }
  } else {
    drawFallback();
  }
  file.close();
}
}  // namespace

void ReadingStatsDetailActivity::onEnter() {
  Activity::onEnter();
  invalidateBaseScreenBuffer();
  resolvedCoverBmpPath.clear();
  coverLoadPending = false;
  selectedStatsItem = 0;
  scrollOffset = 0;
  maxScrollOffset = 0;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  if (const auto* book = findBook(bookPath)) {
    resolvedCoverBmpPath = findFastCoverPath(*book);
    coverLoadPending = resolvedCoverBmpPath.empty();
  }
  requestUpdate();
}

void ReadingStatsDetailActivity::onExit() {
  Activity::onExit();
  freeBaseScreenBuffer();
}

bool ReadingStatsDetailActivity::storeBaseScreenBuffer() {
  const uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeBaseScreenBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  baseScreenBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!baseScreenBuffer) {
    return false;
  }

  memcpy(baseScreenBuffer, frameBuffer, bufferSize);
  baseScreenBufferStored = true;
  baseScreenBookPath = bookPath;
  baseScreenCoverPath = resolvedCoverBmpPath;
  baseScreenScrollOffset = scrollOffset;
  return true;
}

bool ReadingStatsDetailActivity::restoreBaseScreenBuffer() {
  if (!baseScreenBufferStored || !baseScreenBuffer || baseScreenBookPath != bookPath ||
      baseScreenCoverPath != resolvedCoverBmpPath || baseScreenScrollOffset != scrollOffset) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  memcpy(frameBuffer, baseScreenBuffer, renderer.getBufferSize());
  return true;
}

void ReadingStatsDetailActivity::invalidateBaseScreenBuffer() {
  baseScreenBufferStored = false;
  baseScreenBookPath.clear();
  baseScreenCoverPath.clear();
  baseScreenScrollOffset = -1;
}

void ReadingStatsDetailActivity::freeBaseScreenBuffer() {
  if (baseScreenBuffer) {
    free(baseScreenBuffer);
    baseScreenBuffer = nullptr;
  }
  invalidateBaseScreenBuffer();
}

void ReadingStatsDetailActivity::openAdjustment() {
  const auto* book = findBook(bookPath);
  if (book == nullptr) {
    requestUpdate();
    return;
  }

  startActivityForResultWith<BookReadingAdjustmentActivity>(
      [this](const ActivityResult&) {
        guardChildReturn();
        requestUpdate();
      },
      book->path, getDisplayTitle(*book));
}

void ReadingStatsDetailActivity::guardChildReturn() {
  invalidateBaseScreenBuffer();
  waitForBackRelease = true;
  waitForConfirmRelease = true;
}

void ReadingStatsDetailActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  const auto scrollBy = [&](const int delta) {
    const int nextOffset = std::clamp(scrollOffset + delta, 0, maxScrollOffset);
    if (nextOffset == scrollOffset) {
      return false;
    }
    scrollOffset = nextOffset;
    selectedStatsItem = 0;
    invalidateBaseScreenBuffer();
    requestUpdate();
    return true;
  };

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    scrollBy(DETAIL_SCROLL_STEP);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    scrollBy(-DETAIL_SCROLL_STEP);
    return;
  }

  buttonNavigator.onNextPress([&]() {
    if (maxScrollOffset > 0) {
      if (scrollOffset == 0 && selectedStatsItem == 0) {
        selectedStatsItem = DETAIL_ADJUST_FOCUS_INDEX;
        requestUpdate();
        return;
      }
      if (scrollOffset < maxScrollOffset && scrollBy(DETAIL_SCROLL_STEP)) {
        return;
      }
      return;
    }

    selectedStatsItem = ButtonNavigator::nextIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([&]() {
    if (maxScrollOffset > 0) {
      if (scrollOffset > 0 && scrollBy(-DETAIL_SCROLL_STEP)) {
        return;
      }
      selectedStatsItem = ButtonNavigator::previousIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
      requestUpdate();
      return;
    }

    selectedStatsItem = ButtonNavigator::previousIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
    requestUpdate();
  });

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto coverSize = UITheme::getInstance().hasMainTabs() ? InxCoverGeometry::fit(COVER_WIDTH, COVER_HEIGHT)
                                                              : InxCoverGeometry::Size{COVER_WIDTH, COVER_HEIGHT};
  const DetailHeroLayout hero = detailHeroLayout(metrics, contentTop, coverSize.width, coverSize.height, scrollOffset);
  const auto contains = [](const Rect& rect, const int x, const int y) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
  };

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
    const int touchedItem = contains(hero.adjust, touchX, touchY) ? DETAIL_ADJUST_FOCUS_INDEX : 0;
    if ((contains(hero.cover, touchX, touchY) || touchedItem == DETAIL_ADJUST_FOCUS_INDEX) &&
        selectedStatsItem != touchedItem) {
      selectedStatsItem = touchedItem;
      requestUpdate();
    }
  }
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    if (contains(hero.adjust, touchX, touchY)) {
      selectedStatsItem = DETAIL_ADJUST_FOCUS_INDEX;
      openAdjustment();
      return;
    }
    if (contains(hero.cover, touchX, touchY) && Storage.exists(bookPath.c_str())) {
      selectedStatsItem = 0;
      onSelectBook(bookPath);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && selectedStatsItem == DETAIL_ADJUST_FOCUS_INDEX) {
    openAdjustment();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && Storage.exists(bookPath.c_str())) {
    onSelectBook(bookPath);
  }
}

void ReadingStatsDetailActivity::render(RenderLock&&) {
  if (coverLoadPending) {
    coverLoadPending = false;
    if (const auto* pendingBook = findBook(bookPath)) {
      std::string resolvedCoverPath;
      if (FsHelpers::hasEpubExtension(pendingBook->path)) {
        GfxRenderer::FrameBufferLoan loan(renderer);
        resolvedCoverPath = ensureCoverPath(*pendingBook);
      } else {
        resolvedCoverPath = ensureCoverPath(*pendingBook);
      }
      if (!resolvedCoverPath.empty() && resolvedCoverPath != resolvedCoverBmpPath) {
        resolvedCoverBmpPath = std::move(resolvedCoverPath);
        invalidateBaseScreenBuffer();
      }
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  namespace design = RickyDesignUi;
  namespace ds = ricky::ds;
  const int captionHeight = std::max(renderer.getLineHeight(design::CAPTION), renderer.getLineHeight(design::BODY));
  const int valueHeight = std::max(renderer.getLineHeight(design::HEADLINE), renderer.getLineHeight(design::BODY));
  const int metricCardHeight = RickyStatsLayout::summaryCardHeight(valueHeight, captionHeight * 2);
  const int metricCardGap = ds::space::kGutter;
  const int progressBlockHeight = captionHeight + ds::space::k2 + ds::layout::kProgressHeight;
  const int summaryBannerHeight = captionHeight * 3 + ds::space::kCardPadding * 2 + ds::space::k1;
  const int pageWidth = renderer.getScreenWidth();
  const auto* book = findBook(bookPath);
  const auto& lastSessionSnapshot = READING_STATS.getLastSessionSnapshot();
  const bool showCompletionBanner = context.showSessionSummary && lastSessionSnapshot.valid &&
                                    lastSessionSnapshot.path == bookPath && lastSessionSnapshot.completedThisSession;

  if (!book) {
    renderer.clearScreen();
    invalidateBaseScreenBuffer();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS));
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 30,
                      tr(STR_NO_READING_STATS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto coverSize = UITheme::getInstance().hasMainTabs() ? InxCoverGeometry::fit(COVER_WIDTH, COVER_HEIGHT)
                                                              : InxCoverGeometry::Size{COVER_WIDTH, COVER_HEIGHT};
  const DetailHeroLayout hero = detailHeroLayout(metrics, contentTop, coverSize.width, coverSize.height, scrollOffset);
  const Rect coverBaseRect = hero.coverBase;
  const Rect adjustButtonBaseRect = hero.adjustBase;
  const int textX = coverBaseRect.x + coverBaseRect.width + 16;
  const int textWidth = pageWidth - textX - metrics.contentSidePadding;

  int currentY = contentTop + 6;
  const int titleTop = currentY;
  const std::string& title = book->title.empty() ? book->path : book->title;
  const int titleFont = renderer.textFontId(design::bookTitleFontFor(renderer, title.c_str()), title.c_str());
  const auto wrappedTitle = renderer.wrappedText(titleFont, title.c_str(), textWidth, 2, EpdFontFamily::REGULAR);
  currentY += static_cast<int>(wrappedTitle.size()) * renderer.getLineHeight(titleFont);

  const int authorTop = currentY + 4;
  if (!book->author.empty()) {
    currentY += captionHeight + 10;
  } else {
    currentY += 10;
  }

  currentY += 6;
  const int bookProgressTop = currentY;
  currentY += progressBlockHeight + 14;
  const int chapterProgressTop = currentY;
  currentY += progressBlockHeight + 14;
  const int chapterLabelTop = currentY;
  currentY += captionHeight + 6;
  const int chapterTextTop = currentY;
  const std::string currentChapter = book->chapterTitle.empty() ? std::string(tr(STR_NOT_SET)) : book->chapterTitle;
  const auto chapterLines =
      renderer.wrappedText(design::BODY, currentChapter.c_str(), textWidth, 2, EpdFontFamily::REGULAR);
  currentY += static_cast<int>(chapterLines.size()) * captionHeight;

  int cardsTop =
      std::max(adjustButtonBaseRect.y + adjustButtonBaseRect.height, currentY) + metrics.verticalSpacing + 10;
  const int summaryBannerTop = cardsTop;
  if (showCompletionBanner) {
    cardsTop += summaryBannerHeight + SUMMARY_BANNER_GAP;
  }

  const int estimateCardTop = cardsTop + (metricCardHeight + metricCardGap) * 4;
  const int contentBottom = estimateCardTop + metricCardHeight + metrics.verticalSpacing;
  maxScrollOffset = std::max(0, contentBottom - viewportBottom);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);
  const int scrollDy = -scrollOffset;
  const Rect coverRect = offsetRect(coverBaseRect, scrollDy);
  const Rect adjustButtonRect = offsetRect(adjustButtonBaseRect, scrollDy);
  const bool adjustSelected = scrollOffset == 0 && selectedStatsItem == DETAIL_ADJUST_FOCUS_INDEX;

  // A cached 1-bit background cannot restore native gray. The gray path redraws
  // from state and does not allocate an additional framebuffer.
  const bool baseScreenRestored = !renderer.isGrayscale16Active() && restoreBaseScreenBuffer();
  if (!baseScreenRestored) {
    renderer.clearScreen();
    {
      // Clip the scrolling detail content (cover, title, progress blocks, metric
      // cards) to the viewport so elements scrolled past the screen edges are
      // dropped silently instead of logging "Outside range". Masks/header below
      // and the base-screen store stay outside this scope (clip auto-cleared).
      const GfxRenderer::ClipScope clip(renderer, 0, contentTop, pageWidth, viewportBottom - contentTop);

      drawCover(renderer, coverRect, resolvedCoverBmpPath);
      drawAdjustTimeButton(renderer, adjustButtonRect, false);

      currentY = titleTop + scrollDy;
      for (const auto& line : wrappedTitle) {
        design::text(renderer,
                     {static_cast<int16_t>(textX), static_cast<int16_t>(currentY), static_cast<int16_t>(textWidth),
                      static_cast<int16_t>(renderer.getLineHeight(titleFont))},
                     line.c_str(), titleFont);
        currentY += renderer.getLineHeight(titleFont);
      }

      if (!book->author.empty()) {
        design::secondaryText(renderer,
                              {static_cast<int16_t>(textX), static_cast<int16_t>(authorTop + scrollDy),
                               static_cast<int16_t>(textWidth), static_cast<int16_t>(captionHeight)},
                              book->author.c_str());
      }

      drawProgressBlock(renderer, Rect{textX, bookProgressTop + scrollDy, textWidth, progressBlockHeight},
                        tr(STR_BOOK_PROGRESS), book->lastProgressPercent);
      drawProgressBlock(renderer, Rect{textX, chapterProgressTop + scrollDy, textWidth, progressBlockHeight},
                        tr(STR_CHAPTER_PROGRESS), book->chapterProgressPercent);

      design::secondaryText(renderer,
                            {static_cast<int16_t>(textX), static_cast<int16_t>(chapterLabelTop + scrollDy),
                             static_cast<int16_t>(textWidth), static_cast<int16_t>(captionHeight)},
                            tr(STR_CURRENT_CHAPTER));

      currentY = chapterTextTop + scrollDy;
      for (const auto& line : chapterLines) {
        design::text(renderer,
                     {static_cast<int16_t>(textX), static_cast<int16_t>(currentY), static_cast<int16_t>(textWidth),
                      static_cast<int16_t>(captionHeight)},
                     line.c_str(), design::BODY);
        currentY += captionHeight;
      }

      int drawCardsTop = cardsTop + scrollDy;
      if (showCompletionBanner) {
        drawSummaryBanner(renderer,
                          Rect{metrics.contentSidePadding, summaryBannerTop + scrollDy,
                               pageWidth - metrics.contentSidePadding * 2, summaryBannerHeight},
                          tr(STR_BOOK_FINISHED), tr(STR_COMPLETED_THIS_SESSION), true);
      }

      const int cardWidth = (pageWidth - metrics.contentSidePadding * 2 - metricCardGap) / 2;

      drawMetricCard(renderer, Rect{metrics.contentSidePadding, drawCardsTop, cardWidth, metricCardHeight},
                     tr(STR_LAST_SESSION), ReadingStatsAnalytics::formatDurationHm(book->lastSessionMs));
      drawMetricCard(
          renderer,
          Rect{metrics.contentSidePadding + cardWidth + metricCardGap, drawCardsTop, cardWidth, metricCardHeight},
          tr(STR_TOTAL_TIME), ReadingStatsAnalytics::formatDurationHm(book->totalReadingMs));
      drawMetricCard(renderer,
                     Rect{metrics.contentSidePadding, drawCardsTop + metricCardHeight + metricCardGap, cardWidth,
                          metricCardHeight},
                     tr(STR_SESSIONS), std::to_string(book->sessions));
      drawMetricCard(renderer,
                     Rect{metrics.contentSidePadding + cardWidth + metricCardGap,
                          drawCardsTop + metricCardHeight + metricCardGap, cardWidth, metricCardHeight},
                     tr(STR_STATUS), book->completed ? std::string(tr(STR_DONE)) : std::string(tr(STR_IN_PROGRESS)));
      drawMetricCard(renderer,
                     Rect{metrics.contentSidePadding, drawCardsTop + (metricCardHeight + metricCardGap) * 2,
                          pageWidth - metrics.contentSidePadding * 2, metricCardHeight},
                     tr(STR_LAST_READ_DATE), formatDate(book->lastReadAt));
      drawMetricCard(renderer,
                     Rect{metrics.contentSidePadding, drawCardsTop + (metricCardHeight + metricCardGap) * 3,
                          pageWidth - metrics.contentSidePadding * 2, metricCardHeight},
                     tr(STR_START_END_DATE), formatDateRange(book->firstReadAt, getCompletionDateForDisplay(*book)));
      drawMetricCard(renderer,
                     Rect{metrics.contentSidePadding, drawCardsTop + (metricCardHeight + metricCardGap) * 4,
                          pageWidth - metrics.contentSidePadding * 2, metricCardHeight},
                     tr(STR_ESTIMATED_TIME_LEFT), buildEstimatedTimeLeftText(*book));
    }

    renderer.fillRect(0, 0, pageWidth, contentTop, false);
    if (viewportBottom < pageHeight) {
      renderer.fillRect(0, viewportBottom, pageWidth, pageHeight - viewportBottom, false);
    }
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS));

    if (!renderer.isGrayscale16Active()) storeBaseScreenBuffer();
  }

  if (adjustSelected) {
    drawAdjustTimeButton(renderer, adjustButtonRect, true);
  }

  const char* confirmLabel = adjustSelected ? tr(STR_ADJUST) : (Storage.exists(bookPath.c_str()) ? tr(STR_OPEN) : "");
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
