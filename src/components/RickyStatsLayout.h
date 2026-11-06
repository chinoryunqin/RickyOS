#pragma once

#include <algorithm>

#include "Rect.h"
#include "RickyDesignTokens.h"

// Geometry shared by drawing and input; no buffers or owned resources.
namespace RickyStatsLayout {
inline Rect visibleHit(const Rect& rect, const Rect& viewport) {
  const int x = std::max(rect.x, viewport.x);
  const int y = std::max(rect.y, viewport.y);
  return Rect{x, y, std::max(0, std::min(rect.x + rect.width, viewport.x + viewport.width) - x),
              std::max(0, std::min(rect.y + rect.height, viewport.y + viewport.height) - y)};
}
inline int calendarRowHeight(int labelHeight, int pawSize) {
  return std::max<int>(ricky::ds::space::k16, labelHeight + pawSize + ricky::ds::space::k5);
}
inline Rect calendarCell(const Rect& grid, int rowHeight, int index) {
  const int column = grid.width / 7;
  constexpr int gap = ricky::ds::space::kTouchSpacing;
  return Rect{grid.x + index % 7 * column + gap / 2, grid.y + index / 7 * rowHeight + gap / 2,
              std::max(0, column - gap), std::max(0, rowHeight - gap)};
}
inline int dayAtPoint(const Rect& grid, int rowHeight, int leading, unsigned days, int x, int y) {
  if (rowHeight <= 0 || grid.width < 7 || x < grid.x || y < grid.y || x >= grid.x + grid.width ||
      y >= grid.y + grid.height)
    return 0;
  const int index = (y - grid.y) / rowHeight * 7 + (x - grid.x) / (grid.width / 7);
  const int day = index - leading + 1;
  if (day < 1 || day > static_cast<int>(days)) return 0;
  const auto cell = calendarCell(grid, rowHeight, index);
  return x >= cell.x && x < cell.x + cell.width && y >= cell.y && y < cell.y + cell.height ? day : 0;
}
inline int summaryCardHeight(int valueHeight, int labelHeight) {
  return ricky::ds::space::kCardPadding * 2 + valueHeight + ricky::ds::space::kTextStackGap + labelHeight;
}
}  // namespace RickyStatsLayout
