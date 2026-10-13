#include "RickyReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReadingDayDetailActivity.h"
#include "ReadingStatsStore.h"
#include "components/RickyMascot.h"
#include "components/RickyPageUi.h"
#include "components/RickyPaw.h"
#include "components/RickyReadingStreak.h"
#include "components/UITheme.h"
#include "util/TimeUtils.h"

namespace {
namespace fui = freeink::ui;
constexpr uint64_t kMinuteMs = 60ULL * 1000ULL;

uint32_t minutesOf(const uint64_t ms) { return static_cast<uint32_t>(ms / kMinuteMs); }

// "25 分钟" or "1 小时 12 分钟".
std::string duration(const uint64_t ms) {
  const uint32_t minutes = minutesOf(ms);
  char text[48];
  if (minutes < 60)
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MINUTES), static_cast<unsigned>(minutes));
  else if (minutes % 60 == 0)
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_HOURS), static_cast<unsigned>(minutes / 60));
  else
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_HOURS_MINUTES), static_cast<unsigned>(minutes / 60),
             static_cast<unsigned>(minutes % 60));
  return text;
}

// Compact total for a small tile: whole hours once past an hour.
std::string compactDuration(const uint64_t ms) {
  const uint32_t minutes = minutesOf(ms);
  char text[32];
  if (minutes < 60)
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MINUTES), static_cast<unsigned>(minutes));
  else
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_HOURS), static_cast<unsigned>(minutes / 60));
  return text;
}

// Monday-first weekday of a day ordinal (days since 1970-01-01, a Thursday).
int weekdayOf(const uint32_t dayOrdinal) { return static_cast<int>((dayOrdinal + 3) % 7); }

uint64_t msOnDay(const std::vector<ReadingDayStats>& days, const uint32_t dayOrdinal) {
  for (const auto& day : days)
    if (day.dayOrdinal == dayOrdinal) return day.readingMs;
  return 0;
}

// The i-th code point of a short label list such as "一二三四五六日".
std::string nthLabel(const char* labels, const int index) {
  const auto* p = reinterpret_cast<const unsigned char*>(labels);
  for (int i = 0; *p; ++i) {
    const unsigned char* start = p;
    p += *p < 0x80 ? 1 : (*p < 0xE0 ? 2 : (*p < 0xF0 ? 3 : 4));
    if (i == index) return std::string(reinterpret_cast<const char*>(start), p - start);
  }
  return "";
}

// One fact per day, from what the store really holds.
std::string dailyFact(const ReadingStatsStore& stats, const uint32_t today) {
  std::vector<std::string> facts;
  char text[192];
  uint32_t longest = 0;
  for (const auto& session : stats.getSessionLog()) longest = std::max(longest, session.sessionMs);
  if (longest >= kMinuteMs) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_LONGEST), duration(longest).c_str());
    facts.emplace_back(text);
  }
  const ReadingDayStats* best = nullptr;
  for (const auto& day : stats.getReadingDays())
    if (!best || day.readingMs > best->readingMs) best = &day;
  int year = 0;
  unsigned month = 0, dayOfMonth = 0;
  if (best && best->readingMs >= kMinuteMs &&
      TimeUtils::getDateFromDayOrdinal(best->dayOrdinal, year, month, dayOfMonth)) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_BEST_DAY), month, dayOfMonth,
             duration(best->readingMs).c_str());
    facts.emplace_back(text);
  }
  const ReadingBookStats* top = nullptr;
  for (const auto& book : stats.getBooks())
    if (!top || book.totalReadingMs > top->totalReadingMs) top = &book;
  if (top && top->totalReadingMs >= kMinuteMs && !top->title.empty()) {
    std::string title = top->title;
    // Long titles keep their first dozen characters so the fact fits two lines.
    size_t cut = 0;
    int characters = 0;
    for (; cut < title.size() && characters < 12; ++characters) {
      const auto lead = static_cast<unsigned char>(title[cut]);
      cut += lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
    }
    if (cut < title.size()) title = title.substr(0, cut) + "…";
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_TOP_BOOK), title.c_str(),
             duration(top->totalReadingMs).c_str());
    facts.emplace_back(text);
  }
  if (const uint32_t finished = stats.getBooksFinishedCount()) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_FINISHED), static_cast<unsigned>(finished));
    facts.emplace_back(text);
  }
  if (facts.empty()) return tr(STR_RICKY_STATS_FACT_EMPTY);
  return facts[today % facts.size()];
}

// The month's own story, one fact a day; falls back to all-time facts when the month is
// too quiet to say anything.
std::string monthFact(const ReadingStatsStore& stats, const uint32_t monthStart, const unsigned monthDays,
                      const uint32_t previousStart, const unsigned previousDays, const bool monthOver,
                      const uint32_t today) {
  std::vector<std::string> facts;
  char text[192];
  const uint32_t monthEnd = monthStart + monthDays;
  const auto inMonth = [&](const uint32_t day) { return day >= monthStart && day < monthEnd; };

  // The book read most this month.
  const ReadingBookStats* top = nullptr;
  uint64_t topMs = 0;
  for (const auto& book : stats.getBooks()) {
    uint64_t ms = 0;
    for (const auto& day : book.readingDays)
      if (inMonth(day.dayOrdinal)) ms += day.readingMs;
    if (ms > topMs) {
      topMs = ms;
      top = &book;
    }
  }
  if (top && topMs >= kMinuteMs && !top->title.empty()) {
    std::string title = top->title;
    size_t cut = 0;
    int characters = 0;
    for (; cut < title.size() && characters < 10; ++characters) {
      const auto lead = static_cast<unsigned char>(title[cut]);
      cut += lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
    }
    if (cut < title.size()) title = title.substr(0, cut) + "…";
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_BOOK), title.c_str(), duration(topMs).c_str());
    facts.emplace_back(text);
  }

  // The best day, the weekday that carries the month, and the month against the last.
  uint64_t total = 0, previous = 0, byWeekday[7] = {};
  const ReadingDayStats* best = nullptr;
  int daysRead = 0;
  for (const auto& day : stats.getReadingDays()) {
    if (inMonth(day.dayOrdinal)) {
      total += day.readingMs;
      byWeekday[weekdayOf(day.dayOrdinal)] += day.readingMs;
      if (day.readingMs >= readingStreak::kReadDayMs) ++daysRead;
      if (!best || day.readingMs > best->readingMs) best = &day;
    } else if (day.dayOrdinal >= previousStart && day.dayOrdinal < previousStart + previousDays) {
      previous += day.readingMs;
    }
  }
  int year = 0;
  unsigned month = 0, dayOfMonth = 0;
  if (best && best->readingMs >= kMinuteMs &&
      TimeUtils::getDateFromDayOrdinal(best->dayOrdinal, year, month, dayOfMonth)) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_BEST), month, dayOfMonth,
             duration(best->readingMs).c_str());
    facts.emplace_back(text);
  }
  if (daysRead >= 3) {
    const int weekday = static_cast<int>(std::max_element(byWeekday, byWeekday + 7) - byWeekday);
    const std::string name = nthLabel(tr(STR_RICKY_STATS_WEEKDAYS), weekday);
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_WEEKDAY), name.c_str(), name.c_str());
    facts.emplace_back(text);
  }
  // A month still running is only compared once it is ahead; a finished one either way.
  if (previous >= kMinuteMs && total >= previous + kMinuteMs) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_MORE), duration(total - previous).c_str());
    facts.emplace_back(text);
  } else if (monthOver && previous >= total + kMinuteMs) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_LESS), duration(previous - total).c_str());
    facts.emplace_back(text);
  }
  if (facts.empty()) return total < kMinuteMs ? tr(STR_RICKY_STATS_FACT_MONTH_EMPTY) : dailyFact(stats, today);
  return facts[today % facts.size()];
}

// A chevron for the month buttons, `left` pointing back.
void drawChevron(const GfxRenderer& renderer, const int centerX, const int centerY, const bool left) {
  constexpr int kHalf = 10;
  const int tipX = left ? centerX - kHalf / 2 : centerX + kHalf / 2;
  const int tailX = left ? centerX + kHalf / 2 : centerX - kHalf / 2;
  renderer.drawLine(tailX, centerY - kHalf, tipX, centerY, 3, true);
  renderer.drawLine(tipX, centerY, tailX, centerY + kHalf, 3, true);
}

int monthIndexOf(const uint32_t dayOrdinal) {
  int year = 0;
  unsigned month = 0, day = 0;
  TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day);
  return year * 12 + static_cast<int>(month) - 1;
}

uint32_t firstDayOf(const int monthIndex) {
  return TimeUtils::getDayOrdinalForDate(monthIndex / 12, static_cast<unsigned>(monthIndex % 12) + 1, 1);
}

unsigned daysIn(const int monthIndex) {
  return TimeUtils::getDaysInMonth(monthIndex / 12, static_cast<unsigned>(monthIndex % 12) + 1);
}
}  // namespace

const char* RickyReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATS); }

void RickyReadingStatsActivity::onEnter() {
  UiListActivity::onEnter();
  const auto& stats = READING_STATS;
  currentMonth_ = monthIndexOf(stats.getTodayDayOrdinal());
  firstMonth_ = currentMonth_;
  for (const auto& day : stats.getReadingDays())
    if (day.dayOrdinal != 0) firstMonth_ = std::min(firstMonth_, monthIndexOf(day.dayOrdinal));
  viewMonth_ = currentMonth_;
  app.on(
      ACTION_PREVIOUS_MONTH,
      [](const fui::ActionEvent&, void* user) {
        auto* self = static_cast<RickyReadingStatsActivity*>(user);
        self->showMonth(self->viewMonth_ - 1);
      },
      this);
  app.on(
      ACTION_NEXT_MONTH,
      [](const fui::ActionEvent&, void* user) {
        auto* self = static_cast<RickyReadingStatsActivity*>(user);
        self->showMonth(self->viewMonth_ + 1);
      },
      this);
  app.on(
      ACTION_DAY,
      [](const fui::ActionEvent& event, void* user) {
        static_cast<RickyReadingStatsActivity*>(user)->openDay(event.value);
      },
      this);
}

void RickyReadingStatsActivity::showMonth(const int month) {
  const int clamped = std::clamp(month, firstMonth_, currentMonth_);
  if (clamped == viewMonth_) return;
  viewMonth_ = clamped;
  requestUpdate();
}

void RickyReadingStatsActivity::openDay(const int dayOfMonth) {
  if (dayOfMonth < 1 || dayOfMonth > static_cast<int>(daysIn(viewMonth_))) return;
  const uint32_t day = firstDayOf(viewMonth_) + static_cast<uint32_t>(dayOfMonth - 1);
  if (day > READING_STATS.getTodayDayOrdinal()) return;
  app.clearTapFlash();
  startActivityForResultWith<ReadingDayDetailActivity>([this](const ActivityResult&) { requestUpdate(); }, day);
}

bool RickyReadingStatsActivity::handleCustomInput() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::PageBack)) {
    showMonth(viewMonth_ - 1);
    return true;
  }
  if (mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::PageForward)) {
    showMonth(viewMonth_ + 1);
    return true;
  }
  return false;
}

void RickyReadingStatsActivity::buildScreen(UiScreen& screen) {
  const auto bounds = pageContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(bounds.y), 0,
                  static_cast<int16_t>(renderer.getScreenHeight() - bounds.y - bounds.height), 0});
  const auto& theme = screen.theme();
  auto& target = screen.target();
  screen.insetContent(fui::Insets{static_cast<int16_t>(theme.spaceLg), theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int gap = std::max<int>(12, theme.spaceSm);
  const int pad = gap + gap / 2;
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  const int smallHeight = target.lineHeight(theme.smallText.font);
  const int titleHeight = target.lineHeight(theme.titleText.font);
  RickyPageUi::Bold bold(renderer, 1);
  auto small = theme.smallText;
  small.maxLines = 1;
  auto body = theme.bodyText;
  body.maxLines = 1;
  auto strong = body;
  strong.bold = true;
  auto big = theme.titleText;
  big.bold = true;
  big.maxLines = 1;
  auto centered = small;
  centered.align = fui::TextAlign::Center;
  const auto rect = [](const int x, const int y, const int w, const int h) {
    return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                     static_cast<int16_t>(h)};
  };

  const auto& stats = READING_STATS;
  const uint32_t today = stats.getTodayDayOrdinal();
  const uint64_t todayMs = stats.getTodayReadingMs();
  const uint64_t goalMs = rickyStreak::goalMs();
  const bool reached = todayMs >= goalMs;
  char text[160];

  // Today: the dog reacts to the day's reading.
  const auto mood = rickyMascot::moodFor(todayMs, goalMs);
  const int dog = kRickyMascotSmallSize;
  const int heroHeight = std::max(dog, smallHeight * 2 + titleHeight + gap * 2) + pad * 2;
  const auto hero = screen.takeTop(heroHeight, gap);
  RickyPageUi::card(target, hero, false);
  rickyMascot::draw(renderer, mood, true, hero.x + pad, hero.y + (hero.height - dog) / 2);
  const int textX = hero.x + pad + dog + gap * 2;
  const int textWidth = hero.right() - pad - textX;
  int y = hero.y + (hero.height - (smallHeight * 2 + titleHeight + gap * 2)) / 2;
  target.text(rect(textX, y, textWidth, smallHeight), tr(STR_RICKY_STATS_TODAY), small);
  y += smallHeight;
  snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MINUTES), static_cast<unsigned>(minutesOf(todayMs)));
  target.text(rect(textX, y, textWidth, titleHeight), text, big);
  y += titleHeight + gap / 2;
  RickyPageUi::progressBar(target, rect(textX, y, textWidth, 8),
                           static_cast<int>(std::min<uint64_t>(100, todayMs * 100 / goalMs)));
  y += 8 + gap / 2;
  if (todayMs < kMinuteMs)
    snprintf(text, sizeof(text), "%s", tr(STR_RICKY_STATS_IDLE));
  else if (!reached)
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_GOING), static_cast<unsigned>(minutesOf(goalMs - todayMs) + 1));
  else
    snprintf(text, sizeof(text), "%s", tr(STR_RICKY_STATS_DONE));
  target.text(rect(textX, y, textWidth, smallHeight), text, small);

  // The month: one paw print per day read, a hatched one for a rest day.
  const uint32_t monthStart = firstDayOf(viewMonth_);
  const unsigned monthDays = daysIn(viewMonth_);
  readingStreak::Mark marks[31];
  const auto streak = rickyStreak::evaluate(marks, monthStart, monthDays);
  const int leading = weekdayOf(monthStart);
  const int weeks = (leading + static_cast<int>(monthDays) + 6) / 7;

  const int tileHeight = titleHeight + smallHeight + pad * 2;
  const int storyHeight = smallHeight + gap / 2 + bodyHeight * 2 + pad * 2;
  const int headerHeight = 48;
  const int calendarChrome = pad * 2 + headerHeight + gap / 2 + smallHeight + gap / 2 + gap + smallHeight;
  const int rowHeight = std::clamp(
      (static_cast<int>(screen.body().height) - calendarChrome - tileHeight - storyHeight - gap * 2) / weeks, 52, 84);
  const auto calendar = screen.takeTop(calendarChrome + rowHeight * weeks, gap);
  RickyPageUi::card(target, calendar, false);

  // ‹ 2026 年 10 月 ›
  const int headerY = calendar.y + pad;
  snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MONTH_TITLE), viewMonth_ / 12,
           static_cast<unsigned>(viewMonth_ % 12) + 1);
  auto title = strong;
  title.align = fui::TextAlign::Center;
  target.text(rect(calendar.x, headerY + (headerHeight - bodyHeight) / 2, calendar.width, bodyHeight), text, title);
  const int buttonWidth = 64;
  const auto previous = rect(calendar.x + pad / 2, headerY, buttonWidth, headerHeight);
  const auto next = rect(calendar.right() - pad / 2 - buttonWidth, headerY, buttonWidth, headerHeight);
  if (viewMonth_ > firstMonth_) {
    drawChevron(renderer, previous.x + buttonWidth / 2, headerY + headerHeight / 2, true);
    screen.frame().hit(previous, ACTION_PREVIOUS_MONTH, 0, fui::InputTouch);
  }
  if (viewMonth_ < currentMonth_) {
    drawChevron(renderer, next.x + buttonWidth / 2, headerY + headerHeight / 2, false);
    screen.frame().hit(next, ACTION_NEXT_MONTH, 0, fui::InputTouch);
  }

  const int gridX = calendar.x + pad;
  const int column = (calendar.width - pad * 2) / 7;
  const int labelsY = headerY + headerHeight + gap / 2;
  for (int i = 0; i < 7; ++i)
    target.text(rect(gridX + column * i, labelsY, column, smallHeight),
                nthLabel(tr(STR_RICKY_STATS_WEEKDAYS), i).c_str(), centered);
  const int gridY = labelsY + smallHeight + gap / 2;
  const int paw = std::min({36, rowHeight - smallHeight - 6, column - 12});
  for (unsigned d = 0; d < monthDays; ++d) {
    const int cellIndex = leading + static_cast<int>(d);
    const auto cell = rect(gridX + column * (cellIndex % 7), gridY + rowHeight * (cellIndex / 7), column, rowHeight);
    const uint32_t day = monthStart + d;
    auto number = centered;
    if (day > today) number.color = fui::Color::DarkGray;
    number.bold = day == today;
    snprintf(text, sizeof(text), "%u", d + 1);
    target.text(rect(cell.x, cell.y + 2, cell.width, smallHeight), text, number);
    if (day <= today && marks[d] != readingStreak::Mark::None)
      rickyPaw::draw(renderer, cell.x + (cell.width - paw) / 2, cell.y + smallHeight + 4, paw, marks[d]);
    if (day == today) renderer.drawRoundedRect(cell.x + 2, cell.y, cell.width - 4, cell.height - 4, 2, 12, true);
    if (day <= today && msOnDay(stats.getReadingDays(), day) >= kMinuteMs)
      screen.frame().hit(cell, ACTION_DAY, static_cast<int16_t>(d + 1), fui::InputTouch);
  }

  // Legend: what the three prints mean.
  const int legendY = gridY + rowHeight * weeks + gap;
  constexpr int kLegendPaw = 22;
  const readingStreak::Mark legendMarks[3] = {readingStreak::Mark::Goal, readingStreak::Mark::Read,
                                              readingStreak::Mark::Rest};
  const StrId legendNames[3] = {StrId::STR_RICKY_STATS_LEGEND_GOAL, StrId::STR_RICKY_STATS_LEGEND_READ,
                                StrId::STR_RICKY_STATS_LEGEND_REST};
  const int legendItem = (calendar.width - pad * 2) / 3;
  for (int i = 0; i < 3; ++i) {
    const int itemX = gridX + legendItem * i + (legendItem - kLegendPaw - 8 - 64) / 2;
    rickyPaw::draw(renderer, itemX, legendY + (smallHeight - kLegendPaw) / 2, kLegendPaw, legendMarks[i]);
    target.text(rect(itemX + kLegendPaw + 8, legendY, 64, smallHeight), I18N.get(legendNames[i]), small);
  }

  // The month's totals.
  uint64_t monthMs = 0;
  unsigned daysRead = 0;
  for (const auto& day : stats.getReadingDays())
    if (day.dayOrdinal >= monthStart && day.dayOrdinal < monthStart + monthDays) {
      monthMs += day.readingMs;
      if (day.readingMs >= readingStreak::kReadDayMs) ++daysRead;
    }
  unsigned finishedCount = 0;
  for (const auto& book : stats.getBooks())
    if (book.completed && book.completedAt != 0) {
      const uint32_t finishedDay = TimeUtils::getLocalDayOrdinal(book.completedAt);
      if (finishedDay >= monthStart && finishedDay < monthStart + monthDays) ++finishedCount;
    }
  const auto tiles = screen.takeTop(tileHeight, gap);
  const int tileWidth = (tiles.width - gap * 2) / 3;
  char finished[24], days[24];
  snprintf(finished, sizeof(finished), tr(STR_RICKY_STATS_BOOKS_UNIT), finishedCount);
  snprintf(days, sizeof(days), tr(STR_RICKY_STATS_DAYS_UNIT), daysRead);
  const std::string total = compactDuration(monthMs);
  const char* values[3] = {total.c_str(), days, finished};
  const StrId names[3] = {StrId::STR_RICKY_STATS_MONTH_TOTAL, StrId::STR_RICKY_STATS_DAYS,
                          StrId::STR_RICKY_STATS_FINISHED};
  auto bigCentered = big;
  bigCentered.align = fui::TextAlign::Center;
  for (int i = 0; i < 3; ++i) {
    const auto tile = rect(tiles.x + i * (tileWidth + gap), tiles.y, tileWidth, tileHeight);
    RickyPageUi::card(target, tile, false);
    target.text(rect(tile.x, tile.y + pad, tile.width, titleHeight), values[i], bigCentered);
    target.text(rect(tile.x, tile.y + pad + titleHeight, tile.width, smallHeight), I18N.get(names[i]), centered);
  }

  // The streak in words, and one fact about the month.
  const auto story = screen.takeTop(storyHeight, 0);
  RickyPageUi::card(target, story, false);
  if (streak.restCards > 0)
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_STREAK_REST),
             static_cast<unsigned>(rickyStreak::evaluate().current), static_cast<unsigned>(streak.longest),
             static_cast<unsigned>(streak.restCards));
  else
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_STREAK), static_cast<unsigned>(rickyStreak::evaluate().current),
             static_cast<unsigned>(streak.longest));
  target.text(rect(story.x + pad, story.y + pad, story.width - pad * 2, smallHeight), text, small);
  const int previousMonth = viewMonth_ - 1;
  auto fact = body;
  fact.maxLines = 2;
  const std::string factText = monthFact(stats, monthStart, monthDays, firstDayOf(previousMonth), daysIn(previousMonth),
                                         viewMonth_ < currentMonth_, today);
  target.text(rect(story.x + pad, story.y + pad + smallHeight + gap / 2, story.width - pad * 2, bodyHeight * 2),
              factText.c_str(), fact);
}
