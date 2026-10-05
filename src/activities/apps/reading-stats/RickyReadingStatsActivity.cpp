#include "RickyReadingStatsActivity.h"
#ifdef RICKYOS_PRODUCT
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "components/RickyPageUi.h"
#include "components/UITheme.h"
#include "components/icons/rickyMascotIcons.h"
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

void drawMascot(const GfxRenderer& renderer, const uint8_t* bits, const int x, const int y) {
  constexpr int rowBytes = (kRickyMascotSize + 7) / 8;
  for (int row = 0; row < kRickyMascotSize; ++row)
    for (int column = 0; column < kRickyMascotSize; ++column)
      if ((bits[row * rowBytes + column / 8] & (0x80U >> (column % 8))) == 0)
        renderer.drawPixel(x + column, y + row, true);
}
}  // namespace

const char* RickyReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATS); }

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
  const auto rect = [](const int x, const int y, const int w, const int h) {
    return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                     static_cast<int16_t>(h)};
  };

  const auto& stats = READING_STATS;
  const uint32_t today = stats.getTodayDayOrdinal();
  const uint64_t todayMs = stats.getTodayReadingMs();
  const uint64_t goalMs = std::max<uint64_t>(kMinuteMs, getDailyReadingGoalMs());
  const bool reached = todayMs >= goalMs;

  // Today: the dog reacts to the day's reading.
  const int heroHeight = std::max(kRickyMascotSize, smallHeight * 2 + titleHeight + gap * 2) + pad * 2;
  const auto hero = screen.takeTop(heroHeight, gap);
  RickyPageUi::card(target, hero, false);
  const uint8_t* mascot =
      todayMs < kMinuteMs ? ricky_mascot_sleeping : (reached ? ricky_mascot_happy : ricky_mascot_reading);
  drawMascot(renderer, mascot, hero.x + pad, hero.y + (hero.height - kRickyMascotSize) / 2);
  const int textX = hero.x + pad + kRickyMascotSize + gap * 2;
  const int textWidth = hero.right() - pad - textX;
  int y = hero.y + (hero.height - (smallHeight * 2 + titleHeight + gap * 2)) / 2;
  target.text(rect(textX, y, textWidth, smallHeight), tr(STR_RICKY_STATS_TODAY), small);
  y += smallHeight;
  char text[96];
  snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MINUTES), static_cast<unsigned>(minutesOf(todayMs)));
  target.text(rect(textX, y, textWidth, titleHeight), text, big);
  y += titleHeight + gap / 2;
  RickyPageUi::progressBar(target, rect(textX, y, textWidth, 8),
                           static_cast<int>(std::min<uint64_t>(100, todayMs * 100 / goalMs)));
  y += 8 + gap / 2;
  auto line = small;
  line.maxLines = 2;
  if (todayMs < kMinuteMs)
    snprintf(text, sizeof(text), "%s", tr(STR_RICKY_STATS_IDLE));
  else if (!reached)
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_GOING), static_cast<unsigned>(minutesOf(goalMs - todayMs) + 1));
  else
    snprintf(text, sizeof(text), "%s", tr(STR_RICKY_STATS_DONE));
  target.text(rect(textX, y, textWidth, smallHeight * 2), text, line);

  // This week, Monday first; days still to come stay blank.
  // The chart takes whatever height the other cards leave, within reason.
  const int tileHeight = titleHeight + smallHeight + pad * 2;
  const int factHeight = bodyHeight * 2 + pad * 2;
  const int weekChrome = pad * 2 + bodyHeight + gap + gap / 2 + smallHeight + gap;
  const int chartHeight =
      std::clamp(static_cast<int>(screen.body().height) - weekChrome - tileHeight - factHeight - gap * 2,
                 std::max(72, bodyHeight * 3), 320);
  const auto week = screen.takeTop(pad * 2 + bodyHeight + gap + chartHeight + gap / 2 + smallHeight + gap, gap);
  RickyPageUi::card(target, week, false);
  target.text(rect(week.x + pad, week.y + pad, week.width / 3, bodyHeight), tr(STR_RICKY_STATS_WEEK), strong);
  snprintf(text, sizeof(text), tr(STR_RICKY_STATS_STREAK), static_cast<unsigned>(stats.getCurrentStreakDays()),
           static_cast<unsigned>(stats.getMaxStreakDays()));
  auto right = small;
  right.align = fui::TextAlign::Right;
  target.text(rect(week.x + week.width / 3, week.y + pad + (bodyHeight - smallHeight) / 2, week.width * 2 / 3 - pad,
                   smallHeight),
              text, right);
  const uint32_t monday = today - static_cast<uint32_t>(weekdayOf(today));
  uint64_t scale = goalMs;
  for (int i = 0; i < 7; ++i) scale = std::max(scale, msOnDay(stats.getReadingDays(), monday + i));
  const int chartTop = week.y + pad + bodyHeight + gap;
  const int column = (week.width - pad * 2) / 7;
  const int barWidth = std::max(10, column / 2);
  auto centered = small;
  centered.align = fui::TextAlign::Center;
  const auto black = fui::Paint::solid(fui::Color::Black);
  for (int i = 0; i < 7; ++i) {
    const uint32_t day = monday + static_cast<uint32_t>(i);
    const int x = week.x + pad + column * i + (column - barWidth) / 2;
    const uint64_t ms = msOnDay(stats.getReadingDays(), day);
    if (day <= today) {
      if (ms >= kMinuteMs) {
        const int height = std::max(6, static_cast<int>(chartHeight * ms / scale));
        target.fill(rect(x, chartTop + chartHeight - height, barWidth, height), black, 3);
      } else {
        target.fill(rect(x, chartTop + chartHeight - 3, barWidth, 3), fui::Paint::dither(fui::Color::DarkGray));
      }
    }
    auto label = centered;
    label.bold = day == today;
    const int labelY = chartTop + chartHeight + gap / 2;
    target.text(rect(week.x + pad + column * i, labelY, column, smallHeight),
                nthLabel(tr(STR_RICKY_STATS_WEEKDAYS), i).c_str(), label);
    if (day == today) {
      // A dot under today's day.
      const int dot = 6;
      target.fill(rect(week.x + pad + column * i + (column - dot) / 2, labelY + smallHeight + 2, dot, dot), black,
                  dot / 2);
    }
  }

  // Three totals.
  const auto tiles = screen.takeTop(tileHeight, gap);
  const int tileWidth = (tiles.width - gap * 2) / 3;
  char finished[24], days[24];
  snprintf(finished, sizeof(finished), tr(STR_RICKY_STATS_BOOKS_UNIT),
           static_cast<unsigned>(stats.getBooksFinishedCount()));
  snprintf(days, sizeof(days), tr(STR_RICKY_STATS_DAYS_UNIT), static_cast<unsigned>(stats.getReadingDays().size()));
  const std::string total = compactDuration(stats.getTotalReadingMs());
  const char* values[3] = {total.c_str(), finished, days};
  const StrId names[3] = {StrId::STR_RICKY_STATS_TOTAL, StrId::STR_RICKY_STATS_FINISHED, StrId::STR_RICKY_STATS_DAYS};
  auto bigCentered = big;
  bigCentered.align = fui::TextAlign::Center;
  for (int i = 0; i < 3; ++i) {
    const auto tile = rect(tiles.x + i * (tileWidth + gap), tiles.y, tileWidth, tileHeight);
    RickyPageUi::card(target, tile, false);
    target.text(rect(tile.x, tile.y + pad, tile.width, titleHeight), values[i], bigCentered);
    target.text(rect(tile.x, tile.y + pad + titleHeight, tile.width, smallHeight), I18N.get(names[i]), centered);
  }

  // One fact a day.
  const auto factCard = screen.takeTop(factHeight, 0);
  RickyPageUi::card(target, factCard, false);
  auto fact = body;
  fact.maxLines = 2;
  const std::string factText = dailyFact(stats, today);
  // One line sits in the middle of the card; a long fact wraps to two.
  const bool twoLines = target.measureText(fact.font, factText.c_str(), fact).width > factCard.width - pad * 2;
  const int factY = factCard.y + (twoLines ? pad : (factCard.height - bodyHeight) / 2);
  target.text(rect(factCard.x + pad, factY, factCard.width - pad * 2, bodyHeight * (twoLines ? 2 : 1)),
              factText.c_str(), fact);
}
#endif
