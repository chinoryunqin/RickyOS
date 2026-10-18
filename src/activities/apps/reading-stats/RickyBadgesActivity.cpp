#include "RickyBadgesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/RickyBadgeArt.h"
#include "components/RickyBadgeStatus.h"
#include "components/RickyCompactLayout.h"
#include "components/RickyPageUi.h"
#include "util/TimeUtils.h"

namespace {
namespace fui = freeink::ui;

fui::Rect rect(const int x, const int y, const int w, const int h) {
  return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
}
}  // namespace

const char* RickyBadgesActivity::headerTitle() const { return tr(STR_RICKY_BADGES_TITLE); }

void RickyBadgesActivity::buildScreen(UiScreen& screen) {
  if (RickyCompactLayout::enabled) {
    buildCompact(screen);
    return;
  }
  const auto bounds = pageContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(bounds.y), 0,
                  static_cast<int16_t>(renderer.getScreenHeight() - bounds.y - bounds.height), 0});
  const auto& theme = screen.theme();
  auto& target = screen.target();
  screen.insetContent(fui::Insets{static_cast<int16_t>(theme.spaceLg), theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int gap = std::max<int>(12, theme.spaceSm);
  const int smallHeight = target.lineHeight(theme.smallText.font);
  RickyPageUi::Bold bold(renderer, 1);
  auto small = theme.smallText;
  small.maxLines = 1;
  auto centered = small;
  centered.align = fui::TextAlign::Center;
  auto name = centered;
  name.bold = true;
  auto quiet = centered;
  quiet.color = fui::Color::DarkGray;

  const auto statuses = rickyBadgeStatus::evaluate();
  char text[96];

  // 已得 7/19 · next up.
  const auto summary = screen.takeTop(smallHeight + gap, gap);
  snprintf(text, sizeof(text), tr(STR_RICKY_BADGES_COUNT),
           static_cast<unsigned>(rickyBadgeStatus::earnedCount(statuses)), static_cast<unsigned>(rickyBadges::kCount));
  target.text(rect(summary.x, summary.y, summary.width / 2, smallHeight), text, small);
  const size_t next = rickyBadgeStatus::nextToEarn(statuses);
  if (next < rickyBadges::kCount) {
    snprintf(text, sizeof(text), tr(STR_RICKY_BADGES_NEXT), rickyBadgeStatus::nameOf(next),
             static_cast<unsigned>(statuses[next].progress), static_cast<unsigned>(statuses[next].target));
    auto right = small;
    right.align = fui::TextAlign::Right;
    target.text(rect(summary.x + summary.width / 3, summary.y, summary.width * 2 / 3, smallHeight), text, right);
  }

  // Four across; each badge a medallion over its name and one line about it.
  constexpr int kColumns = 4;
  const auto grid = screen.body();
  const int rows = (static_cast<int>(rickyBadges::kCount) + kColumns - 1) / kColumns;
  const int column = grid.width / kColumns;
  const int rowHeight = std::min<int>(grid.height / rows, 200);
  const int medal = std::min({88, column - 24, rowHeight - smallHeight * 3 - gap});
  for (size_t i = 0; i < rickyBadges::kCount; ++i) {
    const auto& status = statuses[i];
    const int x = grid.x + column * static_cast<int>(i % kColumns);
    const int y = grid.y + rowHeight * static_cast<int>(i / kColumns);
    rickyBadgeArt::draw(renderer, rickyBadges::kDefinitions[i].icon, status.unlocked(), x + (column - medal) / 2, y,
                        medal);
    const int textY = y + medal + 6;
    target.text(rect(x, textY, column, smallHeight), rickyBadgeStatus::nameOf(i), name);
    if (status.unlocked()) {
      int year = 0;
      unsigned month = 0, day = 0;
      TimeUtils::getDateFromDayOrdinal(status.unlockedDay, year, month, day);
      snprintf(text, sizeof(text), tr(STR_RICKY_BADGES_EARNED_ON), month, day);
      target.text(rect(x, textY + smallHeight, column, smallHeight), text, quiet);
      continue;
    }
    target.text(rect(x, textY + smallHeight, column, smallHeight), rickyBadgeStatus::hintOf(i), quiet);
    if (status.target > 1 && status.progress > 0) {
      snprintf(text, sizeof(text), tr(STR_RICKY_BADGES_PROGRESS), static_cast<unsigned>(status.progress),
               static_cast<unsigned>(status.target));
      target.text(rect(x, textY + smallHeight * 2, column, smallHeight), text, quiet);
    }
  }
}

void RickyBadgesActivity::buildCompact(UiScreen& screen) {
  namespace c = RickyCompactLayout;
  const auto bounds = pageContentRect();
  screen.setContentMarginFromScreen({static_cast<int16_t>(bounds.y), c::margin,
                                     static_cast<int16_t>(renderer.getScreenHeight() - bounds.y - bounds.height),
                                     c::margin});
  const auto statuses = rickyBadgeStatus::evaluate();
  // Bounded render-only rows (~2 KiB); no per-row heap allocations. FreeInkUI
  // consumes them synchronously and retains only the action rectangles.
  fui::ListItem items[rickyBadges::kCount]{};
  char values[rickyBadges::kCount][48]{};
  for (size_t i = 0; i < rickyBadges::kCount; ++i) {
    items[i].label = rickyBadgeStatus::nameOf(i);
    items[i].subtitle = rickyBadgeStatus::hintOf(i);
    if (statuses[i].unlocked()) {
      int year = 0;
      unsigned month = 0, day = 0;
      TimeUtils::getDateFromDayOrdinal(statuses[i].unlockedDay, year, month, day);
      snprintf(values[i], sizeof(values[i]), tr(STR_RICKY_BADGES_EARNED_ON), month, day);
    } else {
      snprintf(values[i], sizeof(values[i]), "%u/%u", static_cast<unsigned>(statuses[i].progress),
               static_cast<unsigned>(statuses[i].target));
    }
    items[i].value = values[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps props;
  props.items = items;
  props.count = static_cast<uint16_t>(rickyBadges::kCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.subtitleText = screen.theme().smallText;
  props.rowHeight = 96;
  props.rowGap = 8;
  props.sidePadding = 76;
  syncListViewport(screen, props);
  props.partialTrailingRow = false;
  screen.list(props);
  const auto body = screen.body();
  int y = body.y;
  for (int i = props.topIndex; i < props.count && y + props.rowHeight <= body.bottom(); ++i) {
    const bool selected = props.selectedIndex == i;
    for (int py = 0; py < 48; ++py) {
      for (int px = 0; px < 48; ++px) {
        if (rickyBadgeArt::inkAt(rickyBadges::kDefinitions[i].icon, statuses[i].unlocked(), px, py, 48))
          renderer.drawPixel(body.x + 12 + px, y + (props.rowHeight - 48) / 2 + py, !selected);
      }
    }
    y += props.rowHeight + props.rowGap;
  }
}
