#pragma once

#include <cstdint>

#include "RickyStatsActivity.h"

// RickyOS reading statistics: a vertically browsable overview, no menu. Today against the daily goal with the
// logo's dog reacting to it, the month as a calendar of paw prints (the reading streak
// of util/ReadingStreak.h), the month's totals and one small fact that changes daily,
// all from ReadingStatsStore. ‹ › or the page buttons move between months; a day with
// reading opens its detail. The last card links to the badges (or Down) and the month
// report (or Confirm); in a month's first days it announces last month's report.
class RickyReadingStatsActivity final : public RickyStatsActivity {
 public:
  RickyReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : RickyStatsActivity("RickyReadingStats", renderer, input) {}

  void onEnter() override;

 protected:
  const char* headerTitle() const override;
  void buildPage(UiScreen& screen, RickyStatsPage& page) override;
  bool handleStatsInput() override;

 private:
  static constexpr freeink::ui::ActionId ACTION_PREVIOUS_MONTH = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_NEXT_MONTH = ACTION_USER + 1;
  static constexpr freeink::ui::ActionId ACTION_DAY = ACTION_USER + 2;
  static constexpr freeink::ui::ActionId ACTION_BADGES = ACTION_USER + 3;
  static constexpr freeink::ui::ActionId ACTION_REPORT = ACTION_USER + 4;

  // Months as year * 12 + (month - 1), so stepping is plain arithmetic.
  int viewMonth_ = 0;
  int firstMonth_ = 0;
  int currentMonth_ = 0;

  void showMonth(int month);
  void openDay(int dayOfMonth);
  void openTappedDay();
  Rect calendarGrid_;
  int calendarRowHeight_ = 0;
  int calendarLeading_ = 0;
  void openBadges();
  void openReport(int month);
};
