#include "RickyHomeUi.h"

#ifdef RICKYOS_PRODUCT
#include <FsHelpers.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "ReadingStatsStore.h"
#include "RickyHomeLayout.h"
#include "RickyPageUi.h"
#include "RickyProfile.h"
#include "UITheme.h"

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
         : FsHelpers::hasXtcExtension(path)      ? "XTC"
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
  if (painter && painter(owner, index, Rect{rect.x, rect.y, rect.width, rect.height})) return true;
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
  screen.frame().hit(profileRect, OPEN_BOOK, PROFILE, fui::InputTouch);
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
  const int gap = std::max<int>(12, theme.spaceSm);
  const int titleHeight = target.lineHeight(theme.titleText.font);
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  const int smallHeight = target.lineHeight(theme.smallText.font);
  // The content rect already carries the page padding; add only a hairline of air.
  screen.insetContent(fui::Insets{static_cast<int16_t>(gap), 6, static_cast<int16_t>(gap), 6});
  // Hierarchy comes from weight, not gray: 1-bit UI text has no secondary tone.
  RickyPageUi::Bold bold(renderer, 1);
  auto small = theme.smallText;
  small.maxLines = 1;
  auto right = small;
  right.align = fui::TextAlign::Right;
  // Book titles stay on the small face: it carries the full common-character set,
  // so every title renders at one size instead of switching with glyph coverage.
  auto strong = small;
  strong.bold = true;
  const auto rightAction = [&](fui::Rect rect, const char* label, bool arrow) {
    auto mark = rect;
    mark.width = 24;
    mark.x = rect.right() - mark.width;
    rect.width -= mark.width + gap / 2;
    target.text(rect, label, right);
    if (arrow)
      RickyPageUi::arrow(target, mark);
    else
      RickyPageUi::chevron(target, mark);
  };

  // Date and greeting, avatar on the right.
  std::tm local{};
  if (halClock.localTime(local)) {
    static constexpr StrId weekdays[] = {
        StrId::STR_CAL_WEEKDAY_SUN, StrId::STR_CAL_WEEKDAY_MON, StrId::STR_CAL_WEEKDAY_TUE, StrId::STR_CAL_WEEKDAY_WED,
        StrId::STR_CAL_WEEKDAY_THU, StrId::STR_CAL_WEEKDAY_FRI, StrId::STR_CAL_WEEKDAY_SAT};
    snprintf(dateText, sizeof(dateText), tr(STR_RICKY_HOME_DATE), static_cast<unsigned>(local.tm_mon + 1),
             static_cast<unsigned>(local.tm_mday), I18N.get(weekdays[std::clamp(local.tm_wday, 0, 6)]));
  } else {
    snprintf(dateText, sizeof(dateText), "%s", tr(STR_RICKY_DATE_UNSET));
  }
  snprintf(greetingText, sizeof(greetingText), I18N.get(greetingKeys[greetingChoice]), RickyProfile::nickname());
  // A long nickname wraps to a second line instead of losing its end.
  auto greeting = small;
  greeting.maxLines = 2;
  const int avatarSize = std::min<int>(titleHeight + gap / 3 + smallHeight + gap, screen.body().width / 5);
  const int greetingHeight = std::min<int>(
      smallHeight * 2,
      fui::measureWrappedText(target, greetingText, greeting, screen.body().width - avatarSize - gap * 2).height);
  const int introHeight = titleHeight + gap / 3 + greetingHeight;
  const auto profile = screen.takeTop(std::max(introHeight, avatarSize), gap * 2 + gap / 2);
  RickyProfile::drawAvatar(renderer, Rect{profile.right() - avatarSize, profile.y + (profile.height - avatarSize) / 2,
                                          avatarSize, avatarSize});
  auto intro = profile;
  intro.width -= avatarSize + gap * 2;
  intro.y += (profile.height - introHeight) / 2;
  intro.height = titleHeight;
  auto date = theme.titleText;
  date.bold = true;
  date.maxLines = 1;
  {
    RickyPageUi::Bold heading(renderer, 2);
    target.text(intro, dateText, date);
  }
  intro.y += titleHeight + gap / 3;
  intro.height = greetingHeight;
  RickyPageUi::wrappedText(screen.target(), intro, greetingText, greeting);
  screen.frame().hit(profile, OPEN_BOOK, PROFILE, fui::InputTouch);

  // The note to self anchors the bottom of the page.
  auto phrase = theme.bodyText;
  phrase.align = fui::TextAlign::Center;
  phrase.maxLines = 2;
  const int phraseHeight = std::min<int>(
      bodyHeight * 2, fui::measureWrappedText(target, RickyProfile::homePhrase(), phrase, screen.body().width).height);
  const auto note = screen.takeBottom(phraseHeight, gap);
  RickyPageUi::wrappedText(target, note, RickyProfile::homePhrase(), phrase);

  if (!books || books->empty()) {
    screen.centeredText(tr(STR_NO_RECENT_BOOKS));
    screen.frame().hit(screen.body(), OPEN_BOOK, LIBRARY, fui::InputTouch);
    return;
  }

  // Continue reading: generated or real cover, bold title, author, progress.
  target.text(screen.takeTop(smallHeight, gap), start == 0 ? tr(STR_CONTINUE_READING) : tr(STR_MENU_RECENT_BOOKS),
              small);
  // Fixed proportions first, then share what is left between the section gaps
  // so the page breathes evenly instead of collecting space above the note.
  const int labels = smallHeight * 2 + gap / 2 + gap;
  const int tileHeight = smallHeight * 3 + gap;
  const int fixed = smallHeight + gap + gap + smallHeight + gap + tileHeight + labels;
  const int coverWidth =
      std::min<int>(screen.body().width * 27 / 100, std::max(0, screen.body().height - fixed - gap * 4) * 3 / 4);
  const int coverHeight = std::max(smallHeight * 3, coverWidth * 4 / 3);
  const int spare = std::max(0, screen.body().height - fixed - coverHeight - gap * 3);
  const int section = gap + std::min(spare / 3, gap * 2);
  thumbnailHeight = coverHeight;
  const auto row = screen.takeTop(coverHeight, section);
  const auto& book = (*books)[start];
  const fui::Rect cover{row.x, row.y, static_cast<int16_t>(coverHeight * 3 / 4), static_cast<int16_t>(coverHeight)};
  paint(cover, start);
  if (showSelection && selected == start) target.stroke(cover, fui::Paint::solid(fui::Color::Black), 3);
  auto info = row;
  info.x += cover.width + gap * 2;
  info.width -= cover.width + gap * 2;
  auto bookTitle = strong;
  bookTitle.maxLines = 2;
  const int bookTitleHeight =
      std::min<int>(smallHeight * 2, fui::measureWrappedText(target, titleOf(book), bookTitle, info.width).height);
  RickyPageUi::wrappedText(target, fui::Rect{info.x, info.y, info.width, static_cast<int16_t>(bookTitleHeight)},
                           titleOf(book), bookTitle);
  if (!book.author.empty())
    target.text(fui::Rect{info.x, static_cast<int16_t>(info.y + bookTitleHeight + gap / 2), info.width,
                          static_cast<int16_t>(smallHeight)},
                book.author.c_str(), small);
  const int barY = row.bottom() - smallHeight - gap;
  RickyPageUi::progressBar(target, fui::Rect{info.x, static_cast<int16_t>(barY - 6), info.width, 6}, progress);
  auto progressRow = info;
  progressRow.y = row.bottom() - smallHeight;
  progressRow.height = smallHeight;
  snprintf(statsText, sizeof(statsText), tr(STR_RICKY_READ_PROGRESS), static_cast<unsigned>(progress));
  target.text(fui::Rect{progressRow.x, progressRow.y, static_cast<int16_t>(progressRow.width / 2), progressRow.height},
              statsText, small);
  rightAction(fui::Rect{static_cast<int16_t>(progressRow.x + progressRow.width / 2), progressRow.y,
                        static_cast<int16_t>(progressRow.width - progressRow.width / 2), progressRow.height},
              tr(STR_RICKY_HOME_RESUME), true);
  screen.frame().hit(row, OPEN_BOOK, start, fui::InputTouch);

  // Today's reading belongs to the current book, then a hairline closes the block.
  const auto stats = screen.takeTop(smallHeight + gap, section);
  snprintf(statsText, sizeof(statsText), tr(STR_RICKY_HOME_TODAY),
           static_cast<unsigned>(READING_STATS.getTodayReadingMs() / 60000));
  target.text(fui::Rect{stats.x, stats.y, static_cast<int16_t>(stats.width / 2), static_cast<int16_t>(smallHeight)},
              statsText, small);
  snprintf(statsText, sizeof(statsText), tr(STR_RICKY_HOME_STREAK),
           static_cast<unsigned>(READING_STATS.getCurrentStreakDays()));
  rightAction(fui::Rect{static_cast<int16_t>(stats.x + stats.width / 2), stats.y,
                        static_cast<int16_t>(stats.width - stats.width / 2), static_cast<int16_t>(smallHeight)},
              statsText, false);
  target.fill(fui::Rect{stats.x, static_cast<int16_t>(stats.bottom() - 1), stats.width, 1},
              fui::Paint::solid(fui::Color::Black));
  screen.frame().hit(stats, OPEN_BOOK, STATISTICS, fui::InputTouch);

  // Recently opened: landscape tiles keep the two books readable at a glance.
  auto recent = screen.takeTop(smallHeight, gap);
  auto all = recent;
  all.width = recent.width / 3;
  all.x = recent.right() - all.width;
  recent.width -= all.width + gap;
  const int pages = (books->size() + RickyHomeLayout::PAGE_BOOKS - 1) / RickyHomeLayout::PAGE_BOOKS;
  if (pages > 1) {
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d / %d", start / RickyHomeLayout::PAGE_BOOKS + 1, pages);
    auto page = recent;
    page.width = smallHeight * 3;
    page.x = recent.right() - page.width;
    recent.width -= page.width + gap;
    target.text(page, pageText, right);
  }
  target.text(recent, tr(STR_RICKY_HOME_RECENT), small);
  rightAction(all, tr(STR_RICKY_HOME_ALL_BOOKS), true);
  screen.frame().hit(all, OPEN_BOOK, LIBRARY, fui::InputTouch);
  const auto body = screen.body();
  const int count = std::min<int>(2, books->size() - start - 1);
  const int column = gap * 2;
  const int width = (body.width - column) / 2;
  auto previous = RickyPageUi::Motif::Count;
  for (int i = 0; i < count; ++i) {
    const int index = start + i + 1;
    const auto& item = (*books)[index];
    const fui::Rect cell{static_cast<int16_t>(body.x + i * (width + column)), body.y, static_cast<int16_t>(width),
                         static_cast<int16_t>(tileHeight + labels)};
    const fui::Rect tile{cell.x, cell.y, cell.width, static_cast<int16_t>(tileHeight)};
    // Neighbouring tiles never share a motif, even when their titles hash alike.
    auto kind = RickyPageUi::motifFor(titleOf(item));
    if (kind == previous)
      kind =
          static_cast<RickyPageUi::Motif>((static_cast<int>(kind) + 1) % static_cast<int>(RickyPageUi::Motif::Count));
    previous = kind;
    RickyPageUi::bookTile(target, tile, titleOf(item), small, kind);
    if (showSelection && selected == index) target.stroke(tile, fui::Paint::solid(fui::Color::Black), 3);
    auto label =
        fui::Rect{cell.x, static_cast<int16_t>(tile.bottom() + gap), cell.width, static_cast<int16_t>(smallHeight)};
    target.text(label, titleOf(item), strong);
    label.y += smallHeight + gap / 2;
    const auto* saved = READING_STATS.findMatchingBookForPath(item.path);
    if (saved)
      snprintf(statsText, sizeof(statsText), tr(STR_RICKY_READ_PROGRESS),
               static_cast<unsigned>(saved->lastProgressPercent));
    target.text(label, saved ? statsText : tr(STR_RICKY_READ_UNREAD), small);
    screen.frame().hit(cell, OPEN_BOOK, index, fui::InputTouch);
  }
}
#endif
