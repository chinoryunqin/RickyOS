#pragma once
#ifdef RICKYOS_PRODUCT

#include <cstdint>

#include "activities/UiListActivity.h"

// RickyOS reading statistics: one page, no menu. Today against the daily goal with
// the logo's dog reacting to it, this week's days, three totals and one small fact
// that changes daily, all from ReadingStatsStore.
class RickyReadingStatsActivity final : public UiListActivity {
 public:
  RickyReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("RickyReadingStats", renderer, input) {}

 protected:
  int listCount() const override { return 0; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int) override {}
};

#endif
