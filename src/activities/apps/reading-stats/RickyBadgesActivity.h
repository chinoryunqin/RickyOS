#pragma once

#include "RickyStatsActivity.h"

// RickyOS badges (util/RickyBadges.h): every badge as a medallion, earned ones solid with
// the day they were earned, the rest dotted with what they ask for and how far along.
class RickyBadgesActivity final : public RickyStatsActivity {
 public:
  RickyBadgesActivity(GfxRenderer& renderer, MappedInputManager& input)
      : RickyStatsActivity("RickyBadges", renderer, input) {}

 protected:
  const char* headerTitle() const override;
  void buildPage(UiScreen& screen, RickyStatsPage& page) override;
};
