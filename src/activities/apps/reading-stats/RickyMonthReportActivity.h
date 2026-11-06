#pragma once

#include "RickyStatsActivity.h"

// RickyOS month report: one month in a page. The total and how it was read, the month's
// title (全勤书虫, 周末读者, …) with why, the books read most, the week's shape, and the
// badges earned that month or the next one to aim for.
class RickyMonthReportActivity final : public RickyStatsActivity {
 public:
  // month = year * 12 + (month - 1), as rickyMonth uses.
  RickyMonthReportActivity(GfxRenderer& renderer, MappedInputManager& input, int month)
      : RickyStatsActivity("RickyMonthReport", renderer, input), month_(month) {}

 protected:
  const char* headerTitle() const override;
  void buildPage(UiScreen& screen, RickyStatsPage& page) override;

 private:
  int month_;
  mutable char title_[32] = {};
};
