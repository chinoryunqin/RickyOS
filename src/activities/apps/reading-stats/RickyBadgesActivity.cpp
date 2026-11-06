#include "RickyBadgesActivity.h"

#include <I18n.h>

#include <cstdio>

#include "components/RickyBadgeArt.h"
#include "components/RickyBadgeStatus.h"
#include "util/TimeUtils.h"

const char* RickyBadgesActivity::headerTitle() const { return tr(STR_RICKY_BADGES_TITLE); }

void RickyBadgesActivity::buildPage(UiScreen&, RickyStatsPage& page) {
  namespace design = RickyDesignUi;
  namespace fui = freeink::ui;
  const auto rect = RickyStatsPage::rect;
  constexpr int pad = RickyStatsPage::pad, gap = RickyStatsPage::gap, medal = 88;
  const auto statuses = rickyBadgeStatus::evaluate();
  char text[160];
  snprintf(text, sizeof(text), tr(STR_RICKY_BADGES_COUNT),
           static_cast<unsigned>(rickyBadgeStatus::earnedCount(statuses)), static_cast<unsigned>(rickyBadges::kCount));
  auto row = page.take(page.measure(text, design::BODY, page.viewport.width));
  design::text(renderer, row, text, design::BODY, false, fui::TextAlign::Left, 16);
  const size_t next = rickyBadgeStatus::nextToEarn(statuses);
  if (next < rickyBadges::kCount) {
    snprintf(text, sizeof(text), tr(STR_RICKY_BADGES_NEXT), rickyBadgeStatus::nameOf(next),
             static_cast<unsigned>(statuses[next].progress), static_cast<unsigned>(statuses[next].target));
    row = page.take(page.measure(text, design::CAPTION, page.viewport.width));
    design::secondaryText(renderer, row, text, design::CAPTION, false, fui::TextAlign::Left, 16);
  }
  // Two measured columns; row height follows the taller badge, never the
  // remaining screen height. All badges stay reachable at every text scale.
  const int columns = page.viewport.width >= 440 ? 2 : 1;
  const int column = (page.viewport.width - gap * (columns - 1)) / columns;
  const int width = column - pad * 2;
  for (size_t first = 0; first < rickyBadges::kCount; first += columns) {
    int heights[2] = {}, nameHeights[2] = {}, detailHeights[2] = {};
    char details[2][160] = {};
    int rowHeight = 0;
    for (int c = 0; c < columns && first + c < rickyBadges::kCount; ++c) {
      const size_t i = first + c;
      const auto& status = statuses[i];
      if (status.unlocked()) {
        int year = 0;
        unsigned month = 0, day = 0;
        TimeUtils::getDateFromDayOrdinal(status.unlockedDay, year, month, day);
        snprintf(details[c], sizeof(details[c]), tr(STR_RICKY_BADGES_EARNED_ON), month, day);
      } else if (status.target > 1 && status.progress > 0) {
        char progress[48];
        snprintf(progress, sizeof(progress), tr(STR_RICKY_BADGES_PROGRESS), static_cast<unsigned>(status.progress),
                 static_cast<unsigned>(status.target));
        snprintf(details[c], sizeof(details[c]), "%s\n%s", rickyBadgeStatus::hintOf(i), progress);
      } else {
        snprintf(details[c], sizeof(details[c]), "%s", rickyBadgeStatus::hintOf(i));
      }
      nameHeights[c] = page.measure(rickyBadgeStatus::nameOf(i), design::BODY, width);
      detailHeights[c] = page.measure(details[c], design::CAPTION, width);
      heights[c] = pad * 2 + medal + gap + nameHeights[c] + ricky::ds::space::kTextStackGap + detailHeights[c];
      rowHeight = std::max(rowHeight, heights[c]);
    }
    const auto band = page.take(rowHeight);
    for (int c = 0; c < columns && first + c < rickyBadges::kCount; ++c) {
      const size_t i = first + c;
      const auto card = rect(band.x + c * (column + gap), band.y, column, band.height);
      design::softCard(renderer, card, false);
      int y = card.y + pad;
      rickyBadgeArt::draw(renderer, rickyBadges::kDefinitions[i].icon, statuses[i].unlocked(),
                          card.x + (card.width - medal) / 2, y, medal);
      y += medal + gap;
      design::text(renderer, rect(card.x + pad, y, width, nameHeights[c]), rickyBadgeStatus::nameOf(i), design::BODY,
                   false, fui::TextAlign::Center, 16);
      y += nameHeights[c] + ricky::ds::space::kTextStackGap;
      design::secondaryText(renderer, rect(card.x + pad, y, width, detailHeights[c]), details[c], design::CAPTION,
                            false, fui::TextAlign::Center, 16);
    }
  }
}
