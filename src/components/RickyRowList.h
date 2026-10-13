#pragma once

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "RickyDesignTokens.h"
#include "RickyDesignUi.h"
#include "UITheme.h"
#include "UiAppHost.h"

// The 1.2 list row (design spec §8–9): a row inset to the selection bleed, radius 12,
// an optional 32 px icon, the label, a gray value and a trailing chevron on rows that
// open a page or a picker; a description line takes the two-line height. Toggle rows
// keep the shared switch. The reader menu and
// the settings pages draw their lists through here so every list menu looks alike.
// Hits and paging go through the same FreeInkUI calls screen.list() makes.
namespace RickyRowList {

namespace fui = freeink::ui;

struct RowStyle {
  const freeink::Icon* icon = nullptr;
  bool chevron = false;
  const char* value = nullptr;  // nullptr: the item's own value
};

// Row metrics and the content inset; call before syncing the list viewport.
inline void prepare(UiAppHost::UiScreen& screen, fui::ListProps& props) {
  constexpr int inset = ricky::ds::space::kPageMargin - ricky::ds::space::kSelectionBleed;
  screen.insetContent({0, inset, 0, inset});
  props.rowHeight = ricky::ds::layout::kRowHeight;
  props.rowGap = ricky::ds::space::kTouchSpacing;
  props.sidePadding = ricky::ds::space::kInlineGap;
  props.textGap = ricky::ds::space::kInlineGap;
  props.rowInset = 0;
  props.iconSize = 32;
}

// Draws the synced viewport of `props`. `style(i)` describes row i; `actionOffset`
// shifts each row's action value (the reader menu's quick cards come first).
template <typename StyleFn>
void draw(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, fui::ListNav& nav, const fui::ListProps& props,
          StyleFn style, const int actionOffset = 0) {
  const auto body = screen.body();
  int drawn = 0;
  bool selectedDrawn = false;
  int y = body.y;
  for (int i = props.topIndex; i < props.count; ++i) {
    auto item = props.items[i];
    const int16_t height = item.subtitle ? ricky::ds::layout::kRowHeightTwoLine : props.rowHeight;
    if (y + height > body.bottom()) break;
    const fui::Rect row{body.x, static_cast<int16_t>(y), body.width, height};
    y += height + props.rowGap;
    const RowStyle look = style(i);
    if (look.icon) item.icon = fui::bitmapFromIcon(*look.icon);
    const bool selected = UITheme::getInstance().showSelectionCursor() && props.selectedIndex == i;
    selectedDrawn |= props.selectedIndex == i;
    if (item.toggle) {
      auto one = props;
      one.items = &item;
      one.count = 1;
      one.topIndex = 0;
      one.selectedIndex = selected ? 0 : -1;
      one.nav = nullptr;
      one.scrollIndicator = false;
      one.rowHeight = height;
      one.toggleKnobRadius =
          (item.toggleChecked ? ricky::ds::layout::kToggleKnobOn : ricky::ds::layout::kToggleKnobOff) / 2;
      one.toggleKnobInset = (one.toggleHeight - one.toggleKnobRadius * 2) / 2;
      // A one-row list reports actionValue, which the caller set to the row index.
      fui::list(screen.frame(), row, one);
    } else {
      fui::SettingRowProps setting;
      setting.label = item.label;
      setting.subtitle = item.subtitle;
      setting.value = look.value ? look.value : item.value;
      setting.icon = item.icon;
      setting.iconSize = look.icon ? 32 : 0;
      setting.textGap = ricky::ds::space::kInlineGap;
      setting.action = props.action;
      setting.valueId = static_cast<int16_t>(i + actionOffset);
      setting.inputMask = props.inputMask;
      setting.labelText = props.labelText;
      setting.subtitleText = props.subtitleText;
      setting.valueText = screen.theme().smallText;
      setting.valueText.color = fui::Color::DarkGray;
      setting.styles = props.rowStyles;
      setting.state = selected ? fui::StateSelected : fui::StateNormal;
      setting.radius = ricky::ds::radius::kControl;
      setting.sidePadding = props.sidePadding;
      setting.minTouchSize = ricky::ds::layout::kTouchMin;
      setting.drawChevron = look.chevron;
      fui::settingRow(screen.frame(), row, setting);
    }
    ++drawn;
  }
  nav.drawnCount = props.count;
  nav.onListRendered(props.topIndex, drawn, selectedDrawn);
  if (drawn && props.count > drawn) {
    const fui::Rect track{static_cast<int16_t>(body.right() - 2), body.y, 2, body.height};
    RickyDesignUi::fill(renderer, track, ricky::ds::gray::kLine);
    const int height = std::max(12, body.height * drawn / props.count);
    const int y = body.y + (body.height - height) * props.topIndex / (props.count - drawn);
    RickyDesignUi::fill(renderer, {track.x, static_cast<int16_t>(y), 2, static_cast<int16_t>(height)},
                        ricky::ds::gray::kInk);
  }
}

}  // namespace RickyRowList
