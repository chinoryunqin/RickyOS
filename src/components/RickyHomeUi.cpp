#include "RickyHomeUi.h"

#ifdef RICKYOS_PRODUCT
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
  auto title = grid.titleText;
  title.font = rect.width > rect.height * 2 ? fui::GfxRendererTarget::FONT_BODY : fui::GfxRendererTarget::FONT_SMALL;
  if (rect.width > rect.height * 2) {
    uiTarget.fill(rect, fui::Paint::solid(fui::Color::White));
    uiTarget.stroke(rect, fui::Paint::dither(fui::Color::DarkGray), 2);
    const int size = std::min(32, rect.height / 3);
    const int gap = 12;
    RickyPageUi::icon(uiTarget, Rect{rect.x + gap, rect.y + (rect.height - size) / 2, size, size},
                      RickyPageUi::Icon::Book);
    title.maxLines = 2;
    const int height = std::min<int>(rect.height - gap * 2, uiTarget.lineHeight(title.font) * 2);
    uiTarget.text(
        fui::Rect{static_cast<int16_t>(rect.x + size + gap * 2),
                  static_cast<int16_t>(rect.y + (rect.height - height) / 2),
                  static_cast<int16_t>(std::max(0, rect.width - size - gap * 3)), static_cast<int16_t>(height)},
        book.title.empty() ? book.path.c_str() : book.title.c_str(), title);
    return true;
  }
  RickyPageUi::bookPlaceholder(uiTarget, rect, book.title.empty() ? book.path.c_str() : book.title.c_str(), title);
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
  const int gap = std::max<int>(12, theme.spaceSm);
  const int headingHeight = screen.target().lineHeight(theme.bodyText.font);
  const int labelHeight = screen.target().lineHeight(theme.smallText.font);
  screen.insetContent(fui::Insets{static_cast<int16_t>(gap), theme.spaceLg, static_cast<int16_t>(gap), theme.spaceLg});
  auto heading = theme.bodyText;
  heading.bold = true;
  grid.titleText = theme.bodyText;
  snprintf(greetingText, sizeof(greetingText), I18N.get(greetingKeys[greetingChoice]), RickyProfile::nickname());
  auto greeting = theme.smallText;
  greeting.maxLines = 2;
  const int avatarSize = std::min<int>(headingHeight + labelHeight + gap, screen.body().width / 5);
  const int greetingHeight =
      fui::measureWrappedText(screen.target(), greetingText, greeting, screen.body().width - avatarSize - gap * 2)
          .height;
  const auto profile = screen.takeTop(headingHeight + greetingHeight + gap, gap * 2);
  std::tm local{};
  if (halClock.localTime(local)) {
    static constexpr StrId weekdays[] = {
        StrId::STR_CAL_WEEKDAY_SUN, StrId::STR_CAL_WEEKDAY_MON, StrId::STR_CAL_WEEKDAY_TUE, StrId::STR_CAL_WEEKDAY_WED,
        StrId::STR_CAL_WEEKDAY_THU, StrId::STR_CAL_WEEKDAY_FRI, StrId::STR_CAL_WEEKDAY_SAT};
    snprintf(dateText, sizeof(dateText), tr(STR_RICKY_HOME_DATE), static_cast<unsigned>(local.tm_mon + 1),
             static_cast<unsigned>(local.tm_mday), I18N.get(weekdays[std::clamp(local.tm_wday, 0, 6)]));
  } else
    snprintf(dateText, sizeof(dateText), "%s", tr(STR_RICKY_DATE_UNSET));
  RickyProfile::drawAvatar(renderer, Rect{profile.right() - avatarSize, profile.y, avatarSize, avatarSize});
  auto intro = profile;
  intro.width -= avatarSize + gap * 2;
  intro.height = headingHeight;
  screen.target().text(intro, dateText, heading);
  intro.y += headingHeight + gap;
  intro.height = greetingHeight;
  RickyPageUi::wrappedText(screen.target(), intro, greetingText, greeting);
  screen.frame().hit(profile, OPEN_BOOK, PROFILE, fui::InputTouch);

  const auto phraseRect = screen.takeBottom(headingHeight * 2, gap);
  auto phrase = theme.bodyText;
  phrase.align = fui::TextAlign::Center;
  phrase.maxLines = 2;
  auto phraseText = phraseRect;
  const int phraseHeight =
      fui::measureWrappedText(screen.target(), RickyProfile::homePhrase(), phrase, phraseText.width).height;
  phraseText.y += std::max(0, (phraseText.height - phraseHeight) / 2);
  RickyPageUi::wrappedText(screen.target(), phraseText, RickyProfile::homePhrase(), phrase);

  if (!books || books->empty()) {
    screen.centeredText(tr(STR_NO_RECENT_BOOKS));
    screen.frame().hit(screen.body(), OPEN_BOOK, LIBRARY, fui::InputTouch);
    return;
  }
  const auto stats = screen.takeBottom(labelHeight + gap * 2, gap * 2);
  const auto layout =
      RickyHomeLayout::fitPortrait(screen.body().width, screen.body().height, headingHeight, labelHeight, gap);
  const int coverHeight = layout.continueHeight;
  const auto textFit = RickyHomeLayout::fitText(coverHeight - gap - 6, headingHeight, labelHeight, gap, false);
  // One common cache size for both rows. Changing it for every painted card
  // would invalidate all cover states/caches repeatedly during one frame.
  thumbnailHeight = std::max(coverHeight, layout.recentCoverHeight);
  screen.target().text(screen.takeTop(headingHeight, gap),
                       start == 0 ? tr(STR_CONTINUE_READING) : tr(STR_MENU_RECENT_BOOKS), theme.bodyText);
  const auto row = screen.takeTop(coverHeight, gap * 2);
  const auto& book = (*books)[start];
  const fui::Rect cover{row.x, row.y, static_cast<int16_t>(coverHeight * 2 / 3), static_cast<int16_t>(coverHeight)};
  paint(cover, start);
  if (showSelection && selected == start) screen.target().stroke(cover, fui::Paint::solid(fui::Color::Black), 3);
  auto info = row;
  info.x += cover.width + gap * 2;
  info.width -= cover.width + gap * 2;
  const int barY = row.bottom() - labelHeight - gap - 6;
  auto titleRect = info;
  titleRect.height = headingHeight * textFit.titleLines;
  auto bookTitle = heading;
  bookTitle.maxLines = textFit.titleLines;
  RickyPageUi::wrappedText(screen.target(), titleRect, book.title.empty() ? book.path.c_str() : book.title.c_str(),
                           bookTitle);
  if (!book.author.empty() && textFit.author) {
    auto author = info;
    author.y += titleRect.height + gap;
    author.height = std::min(labelHeight, std::max(0, barY - gap - author.y));
    screen.target().text(author, book.author.c_str(), theme.smallText);
  }
  screen.target().fill(fui::Rect{info.x, static_cast<int16_t>(barY), info.width, 6},
                       fui::Paint::dither(fui::Color::LightGray));
  screen.target().fill(
      fui::Rect{info.x, static_cast<int16_t>(barY), static_cast<int16_t>(info.width * progress / 100), 6},
      fui::Paint::solid(fui::Color::Black));
  auto progressRect = info;
  progressRect.y = row.bottom() - labelHeight;
  progressRect.height = labelHeight;
  progressRect.width = info.width / 2;
  snprintf(statsText, sizeof(statsText), tr(STR_RICKY_READ_PROGRESS), static_cast<unsigned>(progress));
  screen.target().text(progressRect, statsText, theme.smallText);
  auto resume = progressRect;
  resume.x = info.x + info.width / 2;
  resume.width = info.width - info.width / 2;
  auto resumeArrow = resume;
  resumeArrow.width = 24;
  resumeArrow.x = resume.right() - resumeArrow.width;
  resume.width -= resumeArrow.width + gap;
  auto right = theme.smallText;
  right.align = fui::TextAlign::Right;
  screen.target().text(resume, tr(STR_RICKY_HOME_RESUME), right);
  RickyPageUi::arrow(screen.target(), resumeArrow);
  screen.frame().hit(row, OPEN_BOOK, start, fui::InputTouch);

  auto leftStats = stats;
  leftStats.width = (stats.width - gap) / 2;
  leftStats.height = labelHeight;
  snprintf(statsText, sizeof(statsText), tr(STR_RICKY_HOME_TODAY),
           static_cast<unsigned>(READING_STATS.getTodayReadingMs() / 60000));
  screen.target().text(leftStats, statsText, theme.smallText);
  auto rightStats = leftStats;
  rightStats.x = stats.right() - rightStats.width;
  auto streakArrow = rightStats;
  streakArrow.width = 24;
  streakArrow.x = rightStats.right() - streakArrow.width;
  rightStats.width -= streakArrow.width + gap;
  snprintf(statsText, sizeof(statsText), tr(STR_RICKY_HOME_STREAK),
           static_cast<unsigned>(READING_STATS.getCurrentStreakDays()));
  screen.target().text(rightStats, statsText, right);
  RickyPageUi::arrow(screen.target(), streakArrow);
  screen.target().fill(fui::Rect{stats.x, static_cast<int16_t>(stats.bottom() - 2), stats.width, 2},
                       fui::Paint::dither(fui::Color::DarkGray));
  screen.frame().hit(stats, OPEN_BOOK, STATISTICS, fui::InputTouch);

  auto recent = screen.takeTop(headingHeight, gap);
  auto all = recent;
  all.width = recent.width / 3;
  all.x = recent.right() - all.width;
  recent.width -= all.width + gap;
  const int pages = (books->size() + RickyHomeLayout::PAGE_BOOKS - 1) / RickyHomeLayout::PAGE_BOOKS;
  if (pages > 1) {
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d / %d", start / RickyHomeLayout::PAGE_BOOKS + 1, pages);
    auto page = recent;
    page.width = headingHeight * 2;
    page.x = recent.right() - page.width;
    recent.width -= page.width + gap;
    screen.target().text(page, pageText, right);
  }
  screen.target().text(recent, tr(STR_RICKY_HOME_RECENT), theme.bodyText);
  screen.frame().hit(all, OPEN_BOOK, LIBRARY, fui::InputTouch);
  auto allArrow = all;
  allArrow.width = 24;
  allArrow.x = all.right() - allArrow.width;
  all.width -= allArrow.width + gap;
  screen.target().text(all, tr(STR_RICKY_HOME_ALL_BOOKS), right);
  RickyPageUi::arrow(screen.target(), allArrow);
  const auto body = screen.body();
  const int count = std::min<int>(2, books->size() - start - 1);
  const int width = layout.cellWidth;
  const int height = layout.recentCoverHeight;
  auto recentTitle = theme.smallText;
  recentTitle.maxLines = 2;
  for (int i = 0; i < count; ++i) {
    const int index = start + i + 1;
    const int coverWidth = height * 2 / 3;
    const fui::Rect cell{static_cast<int16_t>(body.x + i * (width + gap * 2)), body.y, static_cast<int16_t>(width),
                         static_cast<int16_t>(height + labelHeight * 3 + gap + gap / 2)};
    const fui::Rect tile{static_cast<int16_t>(cell.x + (width - coverWidth) / 2), cell.y,
                         static_cast<int16_t>(coverWidth), static_cast<int16_t>(height)};
    paint(tile, index);
    if (showSelection && selected == index) screen.target().stroke(tile, fui::Paint::solid(fui::Color::Black), 3);
    const auto& item = (*books)[index];
    auto label = cell;
    label.y += height + gap;
    label.height = labelHeight * 2;
    RickyPageUi::wrappedText(screen.target(), label, item.title.empty() ? item.path.c_str() : item.title.c_str(),
                             recentTitle);
    label.y += label.height + gap / 2;
    label.height = labelHeight;
    const auto* saved = READING_STATS.findMatchingBookForPath(item.path);
    if (saved)
      snprintf(statsText, sizeof(statsText), tr(STR_RICKY_READ_PROGRESS),
               static_cast<unsigned>(saved->lastProgressPercent));
    screen.target().text(label, saved ? statsText : tr(STR_RICKY_READ_UNREAD), theme.smallText);
    screen.frame().hit(cell, OPEN_BOOK, index, fui::InputTouch);
  }
}
#endif
