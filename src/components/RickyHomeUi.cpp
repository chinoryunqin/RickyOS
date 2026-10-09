#include "RickyHomeUi.h"

#include <FsHelpers.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "ReadingStatsStore.h"
#include "RickyBrandMark.h"
#include "RickyDesignTokens.h"
#include "RickyHomeLayout.h"
#include "RickyPageUi.h"
#include "RickyProfile.h"
#include "UITheme.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId OPEN_BOOK = 1;
constexpr StrId greetingKeys[] = {StrId::STR_RICKY_HOME_GREETING, StrId::STR_RICKY_HOME_GREETING_REST,
                                  StrId::STR_RICKY_HOME_GREETING_SLOW, StrId::STR_RICKY_HOME_GREETING_TIME,
                                  StrId::STR_RICKY_HOME_GREETING_STORY};
static_assert(sizeof(greetingKeys) / sizeof(greetingKeys[0]) == RickyHomeLayout::GREETING_COUNT);
const char* titleOf(const RecentBook& book) { return book.title.empty() ? book.path.c_str() : book.title.c_str(); }
const char* formatOf(const std::string& path) {
  return FsHelpers::hasEpubExtension(path)       ? "EPUB"
         : FsHelpers::hasTxtExtension(path)      ? "TXT"
         : FsHelpers::hasMarkdownExtension(path) ? "MD"
                                                 : nullptr;
}
}  // namespace

void RickyHomeUi::begin(const std::vector<RecentBook>& recent, CoverPainter coverPainter, void* activity) {
  books = &recent;
  painter = coverPainter;
  owner = activity;
  // Non-security variety, chosen once per visit. Render/cache refreshes must
  // never change the greeting or cause a timer-driven e-ink refresh.
  static uint8_t previousGreeting = RickyHomeLayout::GREETING_COUNT;
  static uint32_t visit = 0;
  greetingChoice = RickyHomeLayout::chooseGreeting(static_cast<uint32_t>(halClock.nowUtc()) ^ (++visit * 0x9e3779b9U),
                                                   previousGreeting);
  previousGreeting = greetingChoice;
  resetUi();
  app.on(OPEN_BOOK, &RickyHomeUi::onAction, this);
  app.setScreen(&RickyHomeUi::screenFn, this);
}

void RickyHomeUi::configure(Rect bounds, int selection, bool show, int percent) {
  content = bounds;
  selected = selection;
  start = RickyHomeLayout::pageStart(selection);
  showSelection = show;
  progress = std::clamp(percent, 0, 100);
  snprintf(progressText, sizeof(progressText), "%d%%", progress);
}

void RickyHomeUi::onAction(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<RickyHomeUi*>(user);
  self.pending = event.value;
  self.app.clearTapFlash();
}

int RickyHomeUi::selectedAction(const MappedInputManager& input) {
  pending = -1;
  const auto touch = routeTouch(input);
  return touch.snap.touchReleased ? pending : -1;
}

bool RickyHomeUi::paint(fui::Rect rect, int index) {
  if (painter && painter(owner, index, Rect{rect.x, rect.y, rect.width, rect.height})) {
    // Real artwork gets the same hairline edge as generated covers; pale covers
    // would otherwise dissolve into the page.
    uiTarget.stroke(rect, fui::Paint::solid(fui::Color::Black), 1);
    return true;
  }
  if (!books || index < 0 || index >= static_cast<int>(books->size())) return false;
  const auto& book = (*books)[index];
  auto text = grid.titleText;
  text.font = fui::GfxRendererTarget::FONT_SMALL;  // Full common-character coverage for any title.
  if (rect.width > rect.height * 2) {
    RickyPageUi::bookTile(uiTarget, rect, titleOf(book), text);
    return true;
  }
  RickyPageUi::generatedCover(uiTarget, rect, titleOf(book), book.author.c_str(), formatOf(book.path), text);
  return true;
}

void RickyHomeUi::screenFn(UiScreen& screen, void* user) { static_cast<RickyHomeUi*>(user)->draw(screen); }

void RickyHomeUi::draw(UiScreen& screen) {
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(content.y), static_cast<int16_t>(renderer.getScreenWidth() - content.x - content.width),
      static_cast<int16_t>(renderer.getScreenHeight() - content.y - content.height), static_cast<int16_t>(content.x)});
  if (content.height > content.width) {
    drawPortrait(screen);
    return;
  }
  const auto& theme = screen.theme();
  const int gap = std::max(UiHighDpiProfile::controlGap, static_cast<int>(theme.spaceSm));
  const int16_t padding = gap;
  auto heading = theme.bodyText;
  heading.bold = true;
  const int headingHeight = screen.target().lineHeight(heading.font);
  const int labelHeight = screen.target().lineHeight(theme.smallText.font);
  const bool quietPortrait = content.height > content.width;
  const int profileHeight = headingHeight + labelHeight + gap;
  const auto profileRect = screen.takeTop(profileHeight + (quietPortrait ? labelHeight + gap * 2 : 0), gap * 2);
  std::tm local{};
  if (halClock.localTime(local)) {
    snprintf(dateText, sizeof(dateText), "%04d.%02d.%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  } else {
    snprintf(dateText, sizeof(dateText), "%s", tr(STR_RICKY_DATE_UNSET));
  }
  auto dateStyle = theme.smallText;
  const int avatarSize = std::min<int>(profileHeight, profileRect.width / 4);
  RickyProfile::drawAvatar(renderer, Rect{profileRect.right() - avatarSize, profileRect.y, avatarSize, avatarSize});
  auto textRect = profileRect;
  textRect.width -= avatarSize + gap * 2;
  textRect.height = labelHeight;
  screen.target().text(textRect, dateText, dateStyle);
  textRect.y += labelHeight + gap;
  textRect.height = std::max(0, profileRect.bottom() - textRect.y);
  snprintf(greetingText, sizeof(greetingText), I18N.get(greetingKeys[greetingChoice]), RickyProfile::nickname());
  auto greeting = heading;
  greeting.maxLines = quietPortrait ? 2 : 1;
  screen.target().text(textRect, greetingText, greeting);
  screen.frame().hit(profileRect, OPEN_BOOK, STATISTICS, fui::InputTouch);
  if (quietPortrait) {
    auto phrase = theme.smallText;
    phrase.align = fui::TextAlign::Center;
    phrase.maxLines = 2;
    screen.target().text(screen.takeBottom(labelHeight * 2, gap * 2), RickyProfile::homePhrase(), phrase);
  }
  const auto statsRect = screen.takeBottom(labelHeight + gap * 2, gap * 2);
  snprintf(statsText, sizeof(statsText), tr(STR_RICKY_HOME_STATS),
           static_cast<unsigned>(READING_STATS.getTodayReadingMs() / 60000),
           static_cast<unsigned>(READING_STATS.getCurrentStreakDays()));
  auto statsStyle = theme.smallText;
  statsStyle.align = fui::TextAlign::Center;
  screen.target().text(statsRect, statsText, statsStyle);
  screen.frame().hit(statsRect, OPEN_BOOK, STATISTICS, fui::InputTouch);
  if (!books || books->empty()) {
    auto message = theme.bodyText;
    message.align = fui::TextAlign::Center;
    message.maxLines = 2;
    auto rect = screen.body();
    rect.y += std::max(0, (rect.height - headingHeight * 2) / 2);
    rect.height = headingHeight * 2;
    screen.target().text(rect, tr(STR_NO_RECENT_BOOKS), message);
    screen.frame().hit(rect, OPEN_BOOK, LIBRARY, fui::InputTouch);
    return;
  }
  const auto body = screen.body();
  const auto layout = RickyHomeLayout::fit(body.width, body.height, headingHeight, labelHeight, gap, padding);
  const auto textFit = RickyHomeLayout::fitText(layout.coverHeight, headingHeight, labelHeight, gap, layout.rows == 1);
  thumbnailHeight = layout.coverHeight;
  screen.target().text(screen.takeTop(headingHeight, gap),
                       start == 0 ? tr(STR_CONTINUE_READING) : tr(STR_MENU_RECENT_BOOKS), heading);
  const auto& book = (*books)[start];
  card = {};
  card.title = book.title.empty() ? book.path.c_str() : book.title.c_str();
  card.author = book.author.empty() || !textFit.author ? nullptr : book.author.c_str();
  card.titleText = theme.bodyText;
  card.titleText.maxLines = textFit.titleLines;
  card.authorText = theme.smallText;
  card.progressText = theme.smallText;
  card.progress = progress;
  card.progressMax = textFit.progress ? 100 : 0;
  card.progressLabel = textFit.progress ? progressText : nullptr;
  card.coverSize =
      fui::Size{static_cast<int16_t>(layout.coverHeight * 2 / 3), static_cast<int16_t>(layout.coverHeight)};
  card.padding = fui::Insets{padding, padding, padding, padding};
  card.gap = gap * 2;
  card.textGap = gap;
  card.centerTextOnCover = true;
  card.progressHeight = 6;
  card.action = OPEN_BOOK;
  card.value = start;
  card.state = showSelection && selected == start ? fui::StateSelected : fui::StateNormal;
  card.styles = theme.listRow;
  card.selectionIndicator = fui::BookCardSelectionIndicator::CoverFrame;
  card.coverPainterUserData = this;
  card.coverPainter = [](fui::DrawTarget&, fui::Rect rect, const fui::BookCardProps&, void* user) {
    auto& self = *static_cast<RickyHomeUi*>(user);
    return self.paint(rect, self.start);
  };
  fui::bookCard(screen.frame(), screen.takeTop(layout.rowHeight, gap), card);
  auto recentHeading = screen.takeTop(headingHeight, gap);
  screen.frame().hit(recentHeading, OPEN_BOOK, LIBRARY, fui::InputTouch);
  const int pages = (books->size() + RickyHomeLayout::PAGE_BOOKS - 1) / RickyHomeLayout::PAGE_BOOKS;
  if (pages > 1) {
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d / %d", start / RickyHomeLayout::PAGE_BOOKS + 1, pages);
    auto pageStyle = theme.smallText;
    pageStyle.align = fui::TextAlign::Right;
    auto pageRect = recentHeading;
    pageRect.width = std::min<int>(recentHeading.width / 3, headingHeight * 3);
    pageRect.x += recentHeading.width - pageRect.width;
    screen.target().text(pageRect, pageText, pageStyle);
    recentHeading.width -= pageRect.width + gap;
  }
  screen.target().text(recentHeading, tr(STR_MENU_RECENT_BOOKS), heading);
  grid = {};
  grid.count = std::min<int>(RickyHomeLayout::PAGE_BOOKS - 1, books->size() - start - 1);
  grid.columns = layout.columns;
  grid.rowHeight = layout.rowHeight;
  grid.gap = grid.rowGap = gap;
  grid.coverSize = card.coverSize;
  grid.cellInset = card.padding;
  grid.labelHeight = labelHeight;
  grid.labelGap = 0;
  grid.titleText = theme.smallText;
  grid.titleText.maxLines = 1;
  grid.cellStyles = theme.listRow;
  grid.minTouchSize = 0;  // Keep hit regions inside drawn cells, never expand into gaps.
  grid.action = OPEN_BOOK;
  grid.inputMask = fui::InputTouch;
  grid.selectedIndex = showSelection && selected > start ? selected - start - 1 : -1;
  grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  grid.scrollIndicator = false;
  grid.itemProviderUserData = this;
  grid.itemProvider = [](uint16_t index, void* user) {
    auto& self = *static_cast<RickyHomeUi*>(user);
    const int bookIndex = self.start + index + 1;
    const auto& item = (*self.books)[bookIndex];
    return fui::coverGridItem(item.title.empty() ? item.path.c_str() : item.title.c_str(), bookIndex);
  };
  grid.coverPainterUserData = this;
  grid.coverPainter = [](fui::DrawTarget&, fui::Rect rect, const fui::CoverGridItem&, uint16_t index, void* user) {
    auto& self = *static_cast<RickyHomeUi*>(user);
    return self.paint(rect, self.start + index + 1);
  };
  fui::coverGrid(screen.frame(), screen.body(), grid);
}

void RickyHomeUi::drawPortrait(UiScreen& screen) {
  const auto& theme = screen.theme();
  auto& target = screen.target();
  const int gap = ricky::ds::space::kGutter;
  const int pad = ricky::ds::space::kCardPadding;
  const int radius = ricky::ds::radius::kContainer;
  const int titleHeight = target.lineHeight(theme.titleText.font);
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  const int smallHeight = target.lineHeight(theme.smallText.font);
  auto small = theme.smallText;
  small.maxLines = 1;
  small.bold = false;
  auto title = theme.titleText;
  title.bold = false;
  title.maxLines = 1;
  auto body = theme.bodyText;
  body.bold = false;
  body.maxLines = 1;
  const auto ink = fui::Paint::solid(fui::Color::Black);
  const auto paper = fui::Paint::solid(fui::Color::White);
  const auto cardOf = [&](fui::Rect rect, bool selected) {
    target.fill(rect, selected ? ink : paper, radius);
    if (!selected) target.stroke(rect, ink, ricky::ds::stroke::kHairline, radius);
  };
  const auto styleOn = [&](fui::TextStyle style, bool selected) {
    style.color = selected ? fui::Color::White : fui::Color::Black;
    style.bold = false;
    return style;
  };

  std::tm local{};
  const bool hasClock = halClock.localTime(local);
  static constexpr StrId weekdays[] = {
      StrId::STR_CAL_WEEKDAY_SUN, StrId::STR_CAL_WEEKDAY_MON, StrId::STR_CAL_WEEKDAY_TUE, StrId::STR_CAL_WEEKDAY_WED,
      StrId::STR_CAL_WEEKDAY_THU, StrId::STR_CAL_WEEKDAY_FRI, StrId::STR_CAL_WEEKDAY_SAT};
  if (hasClock) {
    snprintf(dateText, sizeof(dateText), tr(STR_RICKY_HOME_MONTH_DAY), static_cast<unsigned>(local.tm_mon + 1),
             static_cast<unsigned>(local.tm_mday));
    snprintf(statsText, sizeof(statsText), tr(STR_RICKY_HOME_WEEK_STREAK),
             I18N.get(weekdays[std::clamp(local.tm_wday, 0, 6)]),
             static_cast<unsigned>(READING_STATS.getCurrentStreakDays()));
  } else {
    snprintf(dateText, sizeof(dateText), "%s", tr(STR_RICKY_DATE_UNSET));
    snprintf(statsText, sizeof(statsText), "%s", "");
  }
  snprintf(greetingText, sizeof(greetingText), I18N.get(greetingKeys[greetingChoice]), RickyProfile::nickname());

  const int avatarSize = std::min<int>(ricky::ds::layout::kAvatarSize, screen.body().width / 5);
  const int introHeight = titleHeight + smallHeight + bodyHeight + gap;
  const auto profile = screen.takeTop(std::max(introHeight, avatarSize), gap);
  RickyProfile::drawAvatar(renderer, Rect{profile.right() - avatarSize, profile.y, avatarSize, avatarSize});
  auto line = profile;
  line.width -= avatarSize + gap;
  line.height = titleHeight;
  target.text(line, dateText, title);
  line.y += titleHeight;
  line.height = smallHeight;
  if (statsText[0]) target.text(line, statsText, small);
  line.y += smallHeight + gap / 2;
  line.height = bodyHeight;
  target.text(line, greetingText, body);
  screen.frame().hit(fui::Rect{static_cast<int16_t>(profile.right() - avatarSize), profile.y,
                               static_cast<int16_t>(avatarSize), static_cast<int16_t>(avatarSize)},
                     OPEN_BOOK, STATISTICS, fui::InputTouch);

  auto phrase = small;
  phrase.align = fui::TextAlign::Center;
  const auto note = screen.takeBottom(smallHeight, gap);
  target.text(note, RickyProfile::homePhrase(), phrase);

  if (!books || books->empty()) {
    const auto empty = screen.body();
    cardOf(empty, false);
    auto message = body;
    message.align = fui::TextAlign::Center;
    message.maxLines = 2;
    target.text(empty, tr(STR_NO_RECENT_BOOKS), message);
    screen.frame().hit(empty, OPEN_BOOK, LIBRARY, fui::InputTouch);
    return;
  }

  const int statsH = ricky::ds::layout::kSettingsCardHeight;
  const int recentLabels = smallHeight * 2 + gap / 2;
  const int recentHead = smallHeight;
  const int lower = statsH + gap + recentHead + gap + recentLabels;
  int cardH = screen.body().height - lower - gap * 2;
  int recentCover = std::max(smallHeight * 3, (cardH > 280 ? 140 : 96));
  if (cardH < 220) {
    recentCover = smallHeight * 3;
    cardH = std::max(180, screen.body().height - statsH - recentHead - recentCover - recentLabels - gap * 3);
  }
  thumbnailHeight = std::max(1, cardH - pad * 2);

  const auto card = screen.takeTop(cardH, gap);
  const int peek = std::min(88, card.width / 5);
  RickyBrandMark::drawDogPeek(
      renderer, Rect{card.right() - peek - pad, card.y - peek / 2, peek, peek + pad});
  const bool cardSelected = showSelection && selected == start;
  cardOf(card, cardSelected);
  const auto& book = (*books)[start];
  const int coverH = card.height - pad * 2;
  const int coverW = coverH * ricky::ds::layout::kCoverAspectW / ricky::ds::layout::kCoverAspectH;
  const fui::Rect cover{static_cast<int16_t>(card.x + pad), static_cast<int16_t>(card.y + pad),
                        static_cast<int16_t>(coverW), static_cast<int16_t>(coverH)};
  paint(cover, start);
  auto info = card;
  info.x = static_cast<int16_t>(cover.right() + gap);
  info.width = static_cast<int16_t>(card.right() - pad - info.x);
  info.y = static_cast<int16_t>(card.y + pad);
  info.height = smallHeight;
  target.text(info, tr(STR_RICKY_HOME_NOW), styleOn(small, cardSelected));
  info.y += smallHeight + 4;
  auto bookTitle = body;
  bookTitle.maxLines = 2;
  bookTitle.color = cardSelected ? fui::Color::White : fui::Color::Black;
  const int bookTitleHeight =
      std::min<int>(bodyHeight * 2, fui::measureWrappedText(target, titleOf(book), bookTitle, info.width).height);
  info.height = static_cast<int16_t>(bookTitleHeight);
  RickyPageUi::wrappedText(target, info, titleOf(book), bookTitle);
  info.y += bookTitleHeight + 4;
  info.height = smallHeight;
  if (!book.author.empty()) target.text(info, book.author.c_str(), styleOn(small, cardSelected));
  const int barY = card.bottom() - pad - smallHeight - gap - 8;
  target.fill(fui::Rect{info.x, static_cast<int16_t>(barY + 3), info.width, 2},
              fui::Paint::solid(cardSelected ? fui::Color::White : fui::Color::Black));
  const int filled = info.width * std::clamp(progress, 0, 100) / 100;
  if (filled > 0)
    target.fill(fui::Rect{info.x, static_cast<int16_t>(barY), static_cast<int16_t>(filled), 8},
                fui::Paint::solid(cardSelected ? fui::Color::White : fui::Color::Black), 4);
  const auto* saved = READING_STATS.findMatchingBookForPath(book.path);
  if (saved && !saved->chapterTitle.empty())
    snprintf(progressText, sizeof(progressText), "%u%%", static_cast<unsigned>(progress));
  else
    snprintf(progressText, sizeof(progressText), "%u%%", static_cast<unsigned>(progress));
  info.y = static_cast<int16_t>(barY + 10);
  info.height = smallHeight;
  if (saved && !saved->chapterTitle.empty()) {
    snprintf(statsText, sizeof(statsText), "%s", saved->chapterTitle.c_str());
    auto chapter = info;
    chapter.width = static_cast<int16_t>(info.width * 2 / 3);
    target.text(chapter, statsText, styleOn(small, cardSelected));
    auto percent = info;
    percent.x += chapter.width;
    percent.width = static_cast<int16_t>(info.width - chapter.width);
    auto percentStyle = styleOn(small, cardSelected);
    percentStyle.align = fui::TextAlign::Right;
    target.text(percent, progressText, percentStyle);
  } else {
    target.text(info, progressText, styleOn(small, cardSelected));
  }
  const int buttonH = ricky::ds::layout::kButtonCompactHeight;
  const fui::Rect button{info.x, static_cast<int16_t>(card.bottom() - pad - buttonH),
                         static_cast<int16_t>(std::min<int>(info.width, 220)), static_cast<int16_t>(buttonH)};
  target.fill(button, cardSelected ? paper : ink, ricky::ds::radius::kControl);
  auto buttonText = body;
  buttonText.align = fui::TextAlign::Center;
  buttonText.color = cardSelected ? fui::Color::Black : fui::Color::White;
  target.text(button, tr(STR_CONTINUE_READING), buttonText);
  screen.frame().hit(card, OPEN_BOOK, start, fui::InputTouch);

  const auto statsRow = screen.takeTop(statsH, gap);
  const int colGap = ricky::ds::space::kGutter;
  const int colW = (statsRow.width - colGap) / 2;
  const unsigned minutes = static_cast<unsigned>(READING_STATS.getTodayReadingMs() / 60000);
  const unsigned goal = static_cast<unsigned>(getDailyReadingGoalMs() / 60000);
  const unsigned streak = static_cast<unsigned>(READING_STATS.getCurrentStreakDays());
  for (int column = 0; column < 2; ++column) {
    const fui::Rect cell{static_cast<int16_t>(statsRow.x + column * (colW + colGap)), statsRow.y,
                         static_cast<int16_t>(colW), statsRow.height};
    cardOf(cell, false);
    auto number = title;
    number.maxLines = 1;
    auto label = cell;
    label.x += pad;
    label.y += pad;
    label.width -= pad * 2;
    label.height = titleHeight;
    snprintf(statsText, sizeof(statsText), "%u", column == 0 ? minutes : streak);
    target.text(label, statsText, number);
    label.y += titleHeight;
    label.height = smallHeight;
    target.text(label, column == 0 ? tr(STR_RICKY_HOME_MINUTES_UNIT) : tr(STR_RICKY_HOME_DAYS_UNIT), small);
    label.y = static_cast<int16_t>(cell.bottom() - pad - smallHeight);
    if (column == 0) {
      snprintf(statsText, sizeof(statsText), tr(STR_RICKY_HOME_GOAL_LINE), goal);
      target.text(label, statsText, small);
    } else {
      const int barsH = std::max(12, label.y - (cell.y + pad + titleHeight + smallHeight + 8));
      const fui::Rect bars{label.x, static_cast<int16_t>(label.y - barsH - 4), label.width,
                           static_cast<int16_t>(barsH)};
      const uint32_t today = TimeUtils::getLocalDayOrdinal(static_cast<uint32_t>(halClock.nowUtc()));
      uint64_t samples[7] = {};
      uint64_t peak = 1;
      if (today != 0) {
        for (const auto& day : READING_STATS.getReadingDays()) {
          if (day.dayOrdinal > today || today - day.dayOrdinal > 6) continue;
          samples[6 - static_cast<int>(today - day.dayOrdinal)] = day.readingMs;
          peak = std::max(peak, day.readingMs);
        }
      }
      const int barGap = 4;
      const int barW = std::max(2, (bars.width - barGap * 6) / 7);
      for (int i = 0; i < 7; ++i) {
        const int h = samples[i] == 0 ? 2 : std::max(2, static_cast<int>(bars.height * samples[i] / peak));
        target.fill(fui::Rect{static_cast<int16_t>(bars.x + i * (barW + barGap)),
                              static_cast<int16_t>(bars.bottom() - h), static_cast<int16_t>(barW),
                              static_cast<int16_t>(h)},
                    ink, 2);
      }
    }
    screen.frame().hit(cell, OPEN_BOOK, STATISTICS, fui::InputTouch);
  }

  auto recent = screen.takeTop(recentHead, gap / 2);
  auto all = recent;
  all.width = recent.width / 4;
  all.x = recent.right() - all.width;
  recent.width -= all.width + gap;
  target.text(recent, tr(STR_RICKY_HOME_RECENT), small);
  auto allStyle = small;
  allStyle.align = fui::TextAlign::Right;
  target.text(all, tr(STR_RICKY_HOME_ALL), allStyle);
  screen.frame().hit(all, OPEN_BOOK, LIBRARY, fui::InputTouch);

  const auto shelf = screen.body();
  const int count = std::min(3, static_cast<int>(books->size()) - start - 1);
  if (count <= 0) return;
  const int column = ricky::ds::space::kGutter;
  const int width = (shelf.width - column * (count - 1)) / count;
  const int thumbH = std::min(recentCover, std::max(1, shelf.height - recentLabels));
  const int thumbW = thumbH * ricky::ds::layout::kCoverAspectW / ricky::ds::layout::kCoverAspectH;
  auto centered = small;
  centered.align = fui::TextAlign::Center;
  for (int i = 0; i < count; ++i) {
    const int index = start + i + 1;
    const auto& item = (*books)[index];
    const fui::Rect cell{static_cast<int16_t>(shelf.x + i * (width + column)), shelf.y, static_cast<int16_t>(width),
                         shelf.height};
    const fui::Rect thumb{static_cast<int16_t>(cell.x + (width - thumbW) / 2), cell.y, static_cast<int16_t>(thumbW),
                          static_cast<int16_t>(thumbH)};
    paint(thumb, index);
    if (showSelection && selected == index)
      target.fill(fui::Rect{static_cast<int16_t>(thumb.x - 8), static_cast<int16_t>(thumb.y - 8),
                            static_cast<int16_t>(thumb.width + 16), static_cast<int16_t>(thumb.height + 16)},
                  ink, ricky::ds::radius::kControl);
    if (showSelection && selected == index) paint(thumb, index);
    auto label = fui::Rect{cell.x, static_cast<int16_t>(thumb.bottom() + 4), cell.width, static_cast<int16_t>(smallHeight)};
    target.text(label, titleOf(item), centered);
    label.y += smallHeight;
    const auto* itemStats = READING_STATS.findMatchingBookForPath(item.path);
    const char* format = formatOf(item.path);
    if (itemStats && format)
      snprintf(statsText, sizeof(statsText), "%s · %u%%", format,
               static_cast<unsigned>(itemStats->lastProgressPercent));
    else if (itemStats)
      snprintf(statsText, sizeof(statsText), "%u%%", static_cast<unsigned>(itemStats->lastProgressPercent));
    else
      snprintf(statsText, sizeof(statsText), "%s", format ? format : tr(STR_RICKY_READ_UNREAD));
    target.text(label, statsText, centered);
    screen.frame().hit(cell, OPEN_BOOK, index, fui::InputTouch);
  }
}
