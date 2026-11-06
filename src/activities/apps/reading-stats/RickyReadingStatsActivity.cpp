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
#include "RickyBadgesActivity.h"
#include "RickyMonthReportActivity.h"
#include "components/RickyBadgeStatus.h"
#include "components/RickyCompanion.h"
#include "components/RickyDesignUi.h"
#include "components/RickyMascot.h"
#include "components/RickyMonthSummary.h"
#include "components/RickyPageUi.h"
#include "components/RickyPaw.h"
#include "components/RickyReadingStreak.h"
#include "components/RickyStatsLayout.h"
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
std::string monthFact(const ReadingStatsStore& stats, const rickyMonth::Summary& month, const uint32_t today) {
  std::vector<std::string> facts;
  char text[192];
  if (month.topCount > 0 && !month.top[0].book->title.empty()) {
    std::string title = month.top[0].book->title;
    size_t cut = 0;
    int characters = 0;
    for (; cut < title.size() && characters < 10; ++characters) {
      const auto lead = static_cast<unsigned char>(title[cut]);
      cut += lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
    }
    if (cut < title.size()) title = title.substr(0, cut) + "…";
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_BOOK), title.c_str(), duration(month.top[0].ms).c_str());
    facts.emplace_back(text);
  }
  int year = 0;
  unsigned monthNumber = 0, dayOfMonth = 0;
  if (month.bestMs >= kMinuteMs && TimeUtils::getDateFromDayOrdinal(month.bestDay, year, monthNumber, dayOfMonth)) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_BEST), monthNumber, dayOfMonth,
             duration(month.bestMs).c_str());
    facts.emplace_back(text);
  }
  if (const int weekday = rickyMonth::busiestWeekday(month); weekday >= 0) {
    const std::string name = nthLabel(tr(STR_RICKY_STATS_WEEKDAYS), weekday);
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_WEEKDAY), name.c_str(), name.c_str());
    facts.emplace_back(text);
  }
  // A month still running is only compared once it is ahead; a finished one either way.
  if (month.previousMs >= kMinuteMs && month.totalMs >= month.previousMs + kMinuteMs) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_MORE),
             duration(month.totalMs - month.previousMs).c_str());
    facts.emplace_back(text);
  } else if (month.over && month.previousMs >= month.totalMs + kMinuteMs) {
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_LESS),
             duration(month.previousMs - month.totalMs).c_str());
    facts.emplace_back(text);
  }
  if (facts.empty()) return month.totalMs < kMinuteMs ? tr(STR_RICKY_STATS_FACT_MONTH_EMPTY) : dailyFact(stats, today);
  return facts[today % facts.size()];
}

// A finished month's report leads the page for the next month's first three days.
bool reportReadyDays(const uint32_t today) {
  int year = 0;
  unsigned month = 0, day = 0;
  TimeUtils::getDateFromDayOrdinal(today, year, month, day);
  return day <= 3;
}

// A chevron for the month buttons, `left` pointing back.
void drawChevron(const GfxRenderer& renderer, const int centerX, const int centerY, const bool left) {
  constexpr int kHalf = 10;
  const int tipX = left ? centerX - kHalf / 2 : centerX + kHalf / 2;
  const int tailX = left ? centerX + kHalf / 2 : centerX - kHalf / 2;
  renderer.drawLine(tailX, centerY - kHalf, tipX, centerY, 3, true);
  renderer.drawLine(tipX, centerY, tailX, centerY + kHalf, 3, true);
}

}  // namespace

const char* RickyReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATS); }

void RickyReadingStatsActivity::onEnter() {
  RickyStatsActivity::onEnter();
  const auto& stats = READING_STATS;
  currentMonth_ = rickyMonth::indexOf(stats.getTodayDayOrdinal());
  firstMonth_ = currentMonth_;
  for (const auto& day : stats.getReadingDays())
    if (day.dayOrdinal != 0) firstMonth_ = std::min(firstMonth_, rickyMonth::indexOf(day.dayOrdinal));
  viewMonth_ = currentMonth_;
  resetScroll();
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
        (void)event;
        static_cast<RickyReadingStatsActivity*>(user)->openTappedDay();
      },
      this);
  app.on(
      ACTION_BADGES,
      [](const fui::ActionEvent&, void* user) { static_cast<RickyReadingStatsActivity*>(user)->openBadges(); }, this);
  app.on(
      ACTION_REPORT,
      [](const fui::ActionEvent& event, void* user) {
        auto* self = static_cast<RickyReadingStatsActivity*>(user);
        // value 1: the "report is ready" line, which opens last month's.
        self->openReport(event.value == 1 ? self->viewMonth_ - 1 : self->viewMonth_);
      },
      this);
}

void RickyReadingStatsActivity::openBadges() {
  app.clearTapFlash();
  startActivityForResultWith<RickyBadgesActivity>([this](const ActivityResult&) { requestUpdate(); });
}

void RickyReadingStatsActivity::openReport(const int month) {
  app.clearTapFlash();
  startActivityForResultWith<RickyMonthReportActivity>([this](const ActivityResult&) { requestUpdate(); }, month);
}

void RickyReadingStatsActivity::showMonth(const int month) {
  const int clamped = std::clamp(month, firstMonth_, currentMonth_);
  if (clamped == viewMonth_) return;
  closeRouting();
  viewMonth_ = clamped;
  resetScroll();
  requestUpdate();
}

void RickyReadingStatsActivity::openDay(const int dayOfMonth) {
  if (dayOfMonth < 1 || dayOfMonth > static_cast<int>(rickyMonth::length(viewMonth_))) return;
  const uint32_t day = rickyMonth::firstDay(viewMonth_) + static_cast<uint32_t>(dayOfMonth - 1);
  if (day > READING_STATS.getTodayDayOrdinal()) return;
  app.clearTapFlash();
  startActivityForResultWith<ReadingDayDetailActivity>([this](const ActivityResult&) { requestUpdate(); }, day);
}

void RickyReadingStatsActivity::openTappedDay() {
  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  const int day = RickyStatsLayout::dayAtPoint(calendarGrid_, calendarRowHeight_, calendarLeading_,
                                               rickyMonth::length(viewMonth_), x, y);
  if (day > 0 && msOnDay(READING_STATS.getReadingDays(), rickyMonth::firstDay(viewMonth_) + day - 1) >= kMinuteMs)
    openDay(day);
}

bool RickyReadingStatsActivity::handleStatsInput() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::PageBack)) {
    showMonth(viewMonth_ - 1);
    return true;
  }
  if (mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::PageForward)) {
    showMonth(viewMonth_ + 1);
    return true;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    openReport(viewMonth_);
    return true;
  }
  if (mappedInput.wasReleased(Button::Down)) {
    openBadges();
    return true;
  }
  return false;
}

void RickyReadingStatsActivity::buildPage(UiScreen& screen, RickyStatsPage& page) {
  namespace design = RickyDesignUi;
  namespace ds = ricky::ds;
  const auto& viewport = page.viewport;
  const int gap = ds::space::kGutter;
  const int pad = ds::space::kCardPadding;
  const int smallHeight = std::max(renderer.getLineHeight(design::CAPTION), renderer.getLineHeight(design::BODY));
  const int bodyHeight = renderer.getLineHeight(design::BODY);
  const int titleHeight = std::max(renderer.getLineHeight(design::HEADLINE), bodyHeight);
  const auto rect = [](int x, int y, int w, int h) {
    return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                     static_cast<int16_t>(h)};
  };
  const auto measure = [&](const char* value, int font, int width, int lines) {
    return page.measure(value, font, width, lines);
  };
  const auto& stats = READING_STATS;
  const uint32_t today = stats.getTodayDayOrdinal();
  const uint64_t todayMs = stats.getTodayReadingMs();
  const uint64_t goalMs = rickyStreak::goalMs();
  const auto mood = rickyMascot::moodFor(todayMs, goalMs);
  char hint[160];
  if (todayMs < kMinuteMs)
    snprintf(hint, sizeof(hint), "%s",
             RickyCompanion::text(StrId::STR_RICKY_STATS_IDLE, StrId::STR_RICKY_STATS_IDLE_CAT));
  else if (todayMs < goalMs)
    snprintf(hint, sizeof(hint), tr(STR_RICKY_STATS_GOING), static_cast<unsigned>(minutesOf(goalMs - todayMs) + 1));
  else
    snprintf(hint, sizeof(hint), "%s",
             RickyCompanion::text(StrId::STR_RICKY_STATS_DONE, StrId::STR_RICKY_STATS_DONE_CAT));
  const int dog = kRickyMascotSmallSize;
  const int heroTextWidth = std::max(1, viewport.width - pad * 2 - dog - gap);
  const int hintHeight = measure(hint, design::CAPTION, heroTextWidth, 2);
  const int heroTextHeight = smallHeight + ds::space::k1 + titleHeight + ds::space::k2 + ds::layout::kProgressHeight +
                             ds::space::k2 + hintHeight;
  const int heroHeight = std::max(dog, heroTextHeight) + pad * 2;

  const uint32_t monthStart = rickyMonth::firstDay(viewMonth_);
  const unsigned monthDays = rickyMonth::length(viewMonth_);
  readingStreak::Mark marks[31];
  const auto streak = rickyStreak::evaluate(marks, monthStart, monthDays);
  const int leading = weekdayOf(monthStart);
  const int weeks = (leading + static_cast<int>(monthDays) + 6) / 7;
  const int headerHeight = std::max<int>(ds::layout::kTouchMin, bodyHeight);
  const int paw = ds::space::k4;
  const int rowHeight = RickyStatsLayout::calendarRowHeight(smallHeight, paw);
  const int calendarHeight =
      pad * 2 + headerHeight + gap + smallHeight + ds::space::k2 + rowHeight * weeks + gap + smallHeight;
  const auto month = rickyMonth::summarize(viewMonth_);
  const int tileWidth = (viewport.width - gap) / 2;
  const StrId names[] = {StrId::STR_RICKY_STATS_MONTH_TOTAL, StrId::STR_RICKY_STATS_DAYS,
                         StrId::STR_RICKY_STATS_FINISHED};
  int labelHeight = smallHeight;
  for (const auto name : names)
    labelHeight = std::max(labelHeight, measure(I18N.get(name), design::CAPTION, tileWidth - pad * 2, 2));
  const int tileHeight = titleHeight + ds::space::k1 + labelHeight + pad * 2;
  // Full-width total, then two columns; never force three large values into narrow cards.
  const int totalsHeight = tileHeight * 2 + gap;

  char streakText[160];
  const uint32_t streakNow = rickyStreak::evaluate().current;
  if (streak.restCards > 0)
    snprintf(streakText, sizeof(streakText), tr(STR_RICKY_STATS_STREAK_REST), static_cast<unsigned>(streakNow),
             static_cast<unsigned>(streak.longest), static_cast<unsigned>(streak.restCards));
  else
    snprintf(streakText, sizeof(streakText), tr(STR_RICKY_STATS_STREAK), static_cast<unsigned>(streakNow),
             static_cast<unsigned>(streak.longest));
  const bool reportReady = viewMonth_ == currentMonth_ && viewMonth_ > firstMonth_ && reportReadyDays(today);
  char text[160];
  if (reportReady) snprintf(text, sizeof(text), tr(STR_RICKY_REPORT_READY), rickyMonth::monthOf(viewMonth_ - 1));
  const std::string factText = reportReady ? std::string(text) : monthFact(stats, month, today);
  const int storyWidth = viewport.width - pad * 2;
  const int streakHeight = measure(streakText, design::CAPTION, storyWidth, 2);
  const int factHeight = measure(factText.c_str(), design::BODY, storyWidth, 3);
  const int linkHeight = std::max<int>(ds::layout::kTouchMin, bodyHeight + ds::space::k4);
  const int storyHeight = pad * 2 + streakHeight + gap + factHeight + gap + linkHeight * 2 + ds::space::k2;
  const auto take = [&](int height) { return page.take(height); };
  const auto hit = [&](fui::Rect area, fui::ActionId action, int value = 0) { page.hit(area, action, value); };

  const auto hero = take(heroHeight);
  design::softCard(renderer, hero, false);
  rickyMascot::draw(renderer, mood, true, hero.x + pad, hero.y + (hero.height - dog) / 2);
  const int textX = hero.x + pad + dog + gap;
  int y = hero.y + pad;
  design::secondaryText(renderer, rect(textX, y, heroTextWidth, smallHeight), tr(STR_RICKY_STATS_TODAY));
  y += smallHeight + ds::space::k1;
  snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MINUTES), static_cast<unsigned>(minutesOf(todayMs)));
  design::text(renderer, rect(textX, y, heroTextWidth, titleHeight), text, design::HEADLINE);
  y += titleHeight + ds::space::k2;
  design::progress(renderer, rect(textX, y, heroTextWidth, ds::layout::kProgressHeight),
                   static_cast<int>(std::min<uint64_t>(100, todayMs * 100 / goalMs)));
  y += ds::layout::kProgressHeight + ds::space::k2;
  design::secondaryText(renderer, rect(textX, y, heroTextWidth, hintHeight), hint, design::CAPTION, false,
                        fui::TextAlign::Left, 2);

  const auto calendar = take(calendarHeight);
  design::softCard(renderer, calendar, false);
  const int headerY = calendar.y + pad;
  snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MONTH_TITLE), viewMonth_ / 12,
           static_cast<unsigned>(viewMonth_ % 12) + 1);
  const int buttonWidth = ds::layout::kTouchMin;
  design::text(renderer,
               rect(calendar.x + pad + buttonWidth, headerY, calendar.width - pad * 2 - buttonWidth * 2, headerHeight),
               text, design::BODY, false, fui::TextAlign::Center);
  const auto previous = rect(calendar.x + pad, headerY, buttonWidth, headerHeight);
  const auto next = rect(calendar.right() - pad - buttonWidth, headerY, buttonWidth, headerHeight);
  if (viewMonth_ > firstMonth_) {
    drawChevron(renderer, previous.x + buttonWidth / 2, headerY + headerHeight / 2, true);
    hit(previous, ACTION_PREVIOUS_MONTH);
  }
  if (viewMonth_ < currentMonth_) {
    drawChevron(renderer, next.x + buttonWidth / 2, headerY + headerHeight / 2, false);
    hit(next, ACTION_NEXT_MONTH);
  }
  const int gridX = calendar.x + pad;
  const int column = (calendar.width - pad * 2) / 7;
  const int labelsY = headerY + headerHeight + gap;
  for (int i = 0; i < 7; ++i)
    design::secondaryText(renderer, rect(gridX + column * i, labelsY, column, smallHeight),
                          nthLabel(tr(STR_RICKY_STATS_WEEKDAYS), i).c_str(), design::CAPTION, false,
                          fui::TextAlign::Center);
  const int gridY = labelsY + smallHeight + ds::space::k2;
  calendarGrid_ = Rect{gridX, gridY, column * 7, rowHeight * weeks};
  calendarRowHeight_ = rowHeight;
  calendarLeading_ = leading;
  for (unsigned d = 0; d < monthDays; ++d) {
    const auto cell = RickyStatsLayout::calendarCell(calendarGrid_, rowHeight, leading + d);
    const bool current = monthStart + d == today;
    // The entire selected date uses the same paper-on-ink state as other controls.
    if (current)
      design::fill(renderer, rect(cell.x, cell.y, cell.width, cell.height), ds::gray::kInk, ds::radius::kControl);
    snprintf(text, sizeof(text), "%u", d + 1);
    design::text(renderer, rect(cell.x, cell.y + ds::space::k1, cell.width, smallHeight), text, design::CAPTION,
                 current, fui::TextAlign::Center);
    if (monthStart + d <= today && marks[d] != readingStreak::Mark::None) {
      // The existing paw renderer emits black; invert its small mark on today's ink cell.
      if (current) {
        for (int py = 0; py < paw; ++py)
          for (int px = 0; px < paw; ++px)
            if (rickyPaw::inkAt(px, py, paw, marks[d], RickyCompanion::cat()))
              renderer.drawPixel(cell.x + (cell.width - paw) / 2 + px, cell.y + smallHeight + ds::space::k2 + py,
                                 false);
      } else {
        rickyPaw::draw(renderer, cell.x + (cell.width - paw) / 2, cell.y + smallHeight + ds::space::k2, paw, marks[d]);
      }
    }
  }
  // One grid target avoids overflowing the shared 24-entry interaction buffer in a full month.
  hit(rect(calendarGrid_.x, calendarGrid_.y, calendarGrid_.width, calendarGrid_.height), ACTION_DAY);
  const int legendY = gridY + rowHeight * weeks + gap;
  constexpr int legendPaw = 22;
  const readingStreak::Mark legendMarks[] = {readingStreak::Mark::Goal, readingStreak::Mark::Read,
                                             readingStreak::Mark::Rest};
  const StrId legendNames[] = {StrId::STR_RICKY_STATS_LEGEND_GOAL, StrId::STR_RICKY_STATS_LEGEND_READ,
                               StrId::STR_RICKY_STATS_LEGEND_REST};
  const int legendWidth = (calendar.width - pad * 2) / 3;
  for (int i = 0; i < 3; ++i) {
    const int x = gridX + i * legendWidth;
    rickyPaw::draw(renderer, x, legendY + (smallHeight - legendPaw) / 2, legendPaw, legendMarks[i]);
    design::secondaryText(
        renderer, rect(x + legendPaw + ds::space::k2, legendY, legendWidth - legendPaw - ds::space::k2, smallHeight),
        I18N.get(legendNames[i]));
  }

  const auto totals = take(totalsHeight);
  char days[32], finished[32];
  snprintf(days, sizeof(days), tr(STR_RICKY_STATS_DAYS_UNIT), month.daysRead);
  snprintf(finished, sizeof(finished), tr(STR_RICKY_STATS_BOOKS_UNIT), month.finished);
  const std::string total = compactDuration(month.totalMs);
  const char* values[] = {total.c_str(), days, finished};
  for (int i = 0; i < 3; ++i) {
    const auto tile =
        i == 0 ? rect(totals.x, totals.y, totals.width, tileHeight)
               : rect(totals.x + (i - 1) * (tileWidth + gap), totals.y + tileHeight + gap, tileWidth, tileHeight);
    design::softCard(renderer, tile, false);
    design::text(renderer, rect(tile.x + pad, tile.y + pad, tile.width - pad * 2, titleHeight), values[i],
                 design::HEADLINE);
    design::secondaryText(
        renderer, rect(tile.x + pad, tile.y + pad + titleHeight + ds::space::k1, tile.width - pad * 2, labelHeight),
        I18N.get(names[i]), design::CAPTION, false, fui::TextAlign::Left, 2);
  }

  const auto story = take(storyHeight);
  design::softCard(renderer, story, false);
  y = story.y + pad;
  design::secondaryText(renderer, rect(story.x + pad, y, storyWidth, streakHeight), streakText, design::CAPTION, false,
                        fui::TextAlign::Left, 2);
  y += streakHeight + gap;
  design::text(renderer, rect(story.x + pad, y, storyWidth, factHeight), factText.c_str(), design::BODY, false,
               fui::TextAlign::Left, 3);
  y += factHeight + gap;
  const auto statuses = rickyBadgeStatus::evaluate();
  snprintf(text, sizeof(text), tr(STR_RICKY_BADGES_COUNT),
           static_cast<unsigned>(rickyBadgeStatus::earnedCount(statuses)), static_cast<unsigned>(rickyBadges::kCount));
  const auto badges = rect(story.x + pad, y, storyWidth, linkHeight);
  const int countWidth = std::min(storyWidth / 2, renderer.getTextWidth(design::CAPTION, text));
  const int arrowWidth = ds::space::k8;
  design::text(renderer,
               rect(badges.x, badges.y, badges.width - countWidth - arrowWidth - ds::space::kInlineGap, badges.height),
               tr(STR_RICKY_BADGES_TITLE));
  design::secondaryText(renderer, rect(badges.right() - arrowWidth - countWidth, badges.y, countWidth, badges.height),
                        text, design::CAPTION, false, fui::TextAlign::Right);
  RickyPageUi::chevron(screen.target(), rect(badges.right() - ds::space::k6, badges.y, ds::space::k6, badges.height));
  hit(badges, ACTION_BADGES);
  y += linkHeight + ds::space::k2;
  const auto report = rect(story.x + pad, y, storyWidth, linkHeight);
  snprintf(text, sizeof(text), tr(STR_RICKY_REPORT_TITLE),
           rickyMonth::monthOf(reportReady ? viewMonth_ - 1 : viewMonth_));
  design::text(renderer, rect(report.x, report.y, report.width - ds::space::k8, report.height), text);
  // The localized title has no embedded chevron; the row owns its arrow.
  RickyPageUi::chevron(screen.target(), rect(report.right() - ds::space::k6, report.y, ds::space::k6, report.height));
  hit(report, ACTION_REPORT, reportReady ? 1 : 0);
}
