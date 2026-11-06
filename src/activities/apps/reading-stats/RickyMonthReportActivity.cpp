#include "RickyMonthReportActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "components/RickyBadgeArt.h"
#include "components/RickyBadgeStatus.h"
#include "components/RickyMonthSummary.h"

namespace {
namespace fui = freeink::ui;
constexpr uint64_t kMinuteMs = 60ULL * 1000ULL;

// "25 分钟" or "1 小时 12 分钟".
std::string duration(const uint64_t ms) {
  const auto minutes = static_cast<unsigned>(ms / kMinuteMs);
  char text[48];
  if (minutes < 60)
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MINUTES), minutes);
  else if (minutes % 60 == 0)
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_HOURS), minutes / 60);
  else
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_HOURS_MINUTES), minutes / 60, minutes % 60);
  return text;
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
}  // namespace

const char* RickyMonthReportActivity::headerTitle() const {
  snprintf(title_, sizeof(title_), tr(STR_RICKY_REPORT_TITLE), rickyMonth::monthOf(month_));
  return title_;
}

void RickyMonthReportActivity::buildPage(UiScreen&, RickyStatsPage& page) {
  namespace design = RickyDesignUi;
  namespace ds = ricky::ds;
  const auto rect = RickyStatsPage::rect;
  constexpr int pad = RickyStatsPage::pad;
  constexpr int gap = RickyStatsPage::gap;
  const int width = page.viewport.width - pad * 2;
  const auto month = rickyMonth::summarize(month_);
  char text[192];
  // All paragraphs use the same measure/draw role and wrap width. No section
  // is shortened to the screen's remaining height; the document can scroll.
  const auto paragraph = [&](int x, int& y, const char* value, int font = design::CAPTION) {
    const int height = page.measure(value, font, width);
    design::text(renderer, rect(x, y, width, height), value, font, false, fui::TextAlign::Left, 16);
    y += height + gap;
  };
  char summary[160], comparison[160] = {};
  snprintf(summary, sizeof(summary), tr(STR_RICKY_REPORT_SUMMARY), month.daysRead, month.finished,
           static_cast<unsigned>(month.longestStreak));
  if (!month.hadEarlierReading)
    snprintf(comparison, sizeof(comparison), "%s", tr(STR_RICKY_REPORT_FIRST_MONTH));
  else if (month.totalMs >= month.previousMs + kMinuteMs)
    snprintf(comparison, sizeof(comparison), tr(STR_RICKY_STATS_FACT_MONTH_MORE),
             duration(month.totalMs - month.previousMs).c_str());
  else if (month.over && month.previousMs >= month.totalMs + kMinuteMs)
    snprintf(comparison, sizeof(comparison), tr(STR_RICKY_STATS_FACT_MONTH_LESS),
             duration(month.previousMs - month.totalMs).c_str());
  snprintf(text, sizeof(text), tr(STR_RICKY_STATS_MONTH_TITLE), rickyMonth::yearOf(month_),
           rickyMonth::monthOf(month_));
  const std::string total = month.totalMs >= kMinuteMs ? duration(month.totalMs) : tr(STR_RICKY_STATS_FACT_MONTH_EMPTY);
  const int heroHeight = pad * 2 + page.measure(text, design::CAPTION, width) + gap +
                         page.measure(total.c_str(), design::HEADLINE, width) + gap +
                         page.measure(summary, design::CAPTION, width) +
                         (comparison[0] ? gap + page.measure(comparison, design::CAPTION, width) : 0);
  const auto hero = page.take(heroHeight);
  design::softCard(renderer, hero, false);
  int y = hero.y + pad;
  paragraph(hero.x + pad, y, text);
  paragraph(hero.x + pad, y, total.c_str(), design::HEADLINE);
  paragraph(hero.x + pad, y, summary);
  if (comparison[0]) paragraph(hero.x + pad, y, comparison);

  const auto title = rickyMonth::titleOf(month);
  snprintf(text, sizeof(text), I18N.get(title.why), title.value);
  const int labelHeight = page.measure(tr(STR_RICKY_REPORT_NAME_LABEL), design::CAPTION, width);
  const int nameHeight = page.measure(I18N.get(title.name), design::HEADLINE, width);
  const auto named =
      page.take(pad * 2 + labelHeight + gap + nameHeight + gap + page.measure(text, design::CAPTION, width));
  design::softCard(renderer, named, false);
  y = named.y + pad;
  paragraph(named.x + pad, y, tr(STR_RICKY_REPORT_NAME_LABEL));
  paragraph(named.x + pad, y, I18N.get(title.name), design::HEADLINE);
  paragraph(named.x + pad, y, text);

  const int heading = page.measure(tr(STR_RICKY_REPORT_TOP_BOOKS), design::CAPTION, width);
  int booksHeight = pad * 2 + heading + gap;
  // At most three top books; fixed-size heights avoid a per-render heap container.
  int titleHeights[3] = {}, timeHeights[3] = {};
  if (month.topCount == 0) booksHeight += page.measure(tr(STR_RICKY_STATS_FACT_MONTH_EMPTY), design::BODY, width);
  for (int i = 0; i < month.topCount; ++i) {
    titleHeights[i] = page.measure(month.top[i].book->title.c_str(), design::BODY, width);
    const auto time = duration(month.top[i].ms);
    timeHeights[i] = page.measure(time.c_str(), design::CAPTION, width);
    booksHeight += titleHeights[i] + ds::space::kTextStackGap + timeHeights[i] + gap + ds::layout::kProgressHeight;
    if (i + 1 < month.topCount) booksHeight += ds::space::kBlockGap;
  }
  const auto books = page.take(booksHeight);
  design::softCard(renderer, books, false);
  y = books.y + pad;
  paragraph(books.x + pad, y, tr(STR_RICKY_REPORT_TOP_BOOKS));
  if (month.topCount == 0) paragraph(books.x + pad, y, tr(STR_RICKY_STATS_FACT_MONTH_EMPTY), design::BODY);
  for (int i = 0; i < month.topCount; ++i) {
    const auto& top = month.top[i];
    design::text(renderer, rect(books.x + pad, y, width, titleHeights[i]), top.book->title.c_str(), design::BODY, false,
                 fui::TextAlign::Left, 16);
    y += titleHeights[i] + ds::space::kTextStackGap;
    design::secondaryText(renderer, rect(books.x + pad, y, width, timeHeights[i]), duration(top.ms).c_str(),
                          design::CAPTION, false, fui::TextAlign::Left, 16);
    y += timeHeights[i] + gap;
    design::progress(renderer, rect(books.x + pad, y, width, ds::layout::kProgressHeight),
                     static_cast<int>(top.ms * 100 / std::max<uint64_t>(1, month.top[0].ms)));
    y += ds::layout::kProgressHeight + ds::space::kBlockGap;
  }

  constexpr int chart = ds::space::k16;
  const int weekHeading = page.measure(tr(STR_RICKY_REPORT_WEEK), design::CAPTION, width);
  const int labelLine = std::max(page.lineHeight(design::CAPTION), page.lineHeight(design::BODY));
  const int busiest = rickyMonth::busiestWeekday(month);
  text[0] = '\0';
  if (busiest >= 0) {
    const auto day = nthLabel(tr(STR_RICKY_STATS_WEEKDAYS), busiest);
    snprintf(text, sizeof(text), tr(STR_RICKY_STATS_FACT_MONTH_WEEKDAY), day.c_str(), day.c_str());
  }
  const auto week = page.take(pad * 2 + weekHeading + gap + chart + ds::space::k2 + labelLine +
                              (text[0] ? gap + page.measure(text, design::CAPTION, width) : 0));
  design::softCard(renderer, week, false);
  y = week.y + pad;
  paragraph(week.x + pad, y, tr(STR_RICKY_REPORT_WEEK));
  const uint64_t peak = std::max<uint64_t>(1, *std::max_element(month.byWeekday, month.byWeekday + 7));
  const int column = width / 7;
  for (int i = 0; i < 7; ++i) {
    const int height = month.byWeekday[i] ? std::max(4, static_cast<int>(chart * month.byWeekday[i] / peak)) : 3;
    const int barWidth = column / 2;
    design::fill(renderer,
                 rect(week.x + pad + column * i + (column - barWidth) / 2, y + chart - height, barWidth, height),
                 month.byWeekday[i] ? ds::gray::kInk : ds::gray::kLine, ds::radius::kControl);
    design::text(renderer, rect(week.x + pad + column * i, y + chart + ds::space::k2, column, labelLine),
                 nthLabel(tr(STR_RICKY_STATS_WEEKDAYS), i).c_str(), design::CAPTION, false, fui::TextAlign::Center);
  }
  y += chart + ds::space::k2 + labelLine + gap;
  if (text[0]) paragraph(week.x + pad, y, text);

  const auto statuses = rickyBadgeStatus::evaluate();
  bool earned = false;
  const int badgeHeading = page.measure(tr(STR_RICKY_REPORT_BADGES), design::CAPTION, page.viewport.width);
  const auto headingArea = page.take(badgeHeading);
  design::secondaryText(renderer, headingArea, tr(STR_RICKY_REPORT_BADGES), design::CAPTION, false,
                        fui::TextAlign::Left, 16);
  constexpr int medal = 64;
  for (size_t i = 0; i < rickyBadges::kCount; ++i) {
    if (!statuses[i].unlocked() || !month.contains(statuses[i].unlockedDay)) continue;
    earned = true;
    const int textWidth = width - medal - gap;
    const int height = page.measure(rickyBadgeStatus::nameOf(i), design::BODY, textWidth);
    const auto row = page.take(pad * 2 + std::max(medal, height));
    design::softCard(renderer, row, false);
    rickyBadgeArt::draw(renderer, rickyBadges::kDefinitions[i].icon, true, row.x + pad,
                        row.y + (row.height - medal) / 2, medal);
    design::text(renderer, rect(row.x + pad + medal + gap, row.y + (row.height - height) / 2, textWidth, height),
                 rickyBadgeStatus::nameOf(i), design::BODY, false, fui::TextAlign::Left, 16);
  }
  if (!earned) {
    const size_t next = rickyBadgeStatus::nextToEarn(statuses);
    if (next < rickyBadges::kCount) {
      snprintf(text, sizeof(text), tr(STR_RICKY_REPORT_NEXT_BADGE), rickyBadgeStatus::nameOf(next),
               static_cast<unsigned>(statuses[next].progress), static_cast<unsigned>(statuses[next].target));
      const auto row = page.take(pad * 2 + page.measure(text, design::CAPTION, width));
      design::softCard(renderer, row, false);
      y = row.y + pad;
      paragraph(row.x + pad, y, text);
    }
  }
}
