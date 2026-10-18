#pragma once

#include "activities/UiListActivity.h"
#include "components/RickyCompactLayout.h"

// RickyOS month report: one month in a page. The total and how it was read, the month's
// title (全勤书虫, 周末读者, …) with why, the books read most, the week's shape, and the
// badges earned that month or the next one to aim for.
class RickyMonthReportActivity final : public UiListActivity {
 public:
  // month = year * 12 + (month - 1), as rickyMonth uses.
  RickyMonthReportActivity(GfxRenderer& renderer, MappedInputManager& input, int month)
      : UiListActivity("RickyMonthReport", renderer, input), month_(month) {}

 protected:
  int listCount() const override { return RickyCompactLayout::enabled ? compactCount_ : 0; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void buildCompact(UiScreen& screen);
  void activateIndex(int) override {}

 private:
  int month_;
  int compactCount_ = 6;
  mutable char title_[32] = {};
};
