#pragma once

#include <FreeInkUICore.h>

#include <algorithm>

// A4 uses the same UI and action IDs as Pico, with a separate space budget.
// Geometry is stack-only and shared by drawing, touch targets and host checks.
namespace RickyCompactLayout {
namespace fui = freeink::ui;
#if (defined(FREEINK_DEVICE_EEGO_A4) && FREEINK_DEVICE_EEGO_A4) || defined(SIMULATOR_DEVICE_EEGO_A4)
inline constexpr bool enabled = true;
#else
inline constexpr bool enabled = false;
#endif
inline constexpr int margin = 20;
inline constexpr int gap = 12;
inline constexpr int padding = 12;

inline fui::Rect box(int x, int y, int width, int height) {
  return {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(std::max(0, width)),
          static_cast<int16_t>(std::max(0, height))};
}
inline fui::Rect top(fui::Rect& body, int height, int after = gap) {
  const auto result = box(body.x, body.y, body.width, std::min<int>(body.height, height));
  const int used = std::min<int>(body.height, result.height + after);
  body.y += used;
  body.height -= used;
  return result;
}
struct Home {
  fui::Rect profile, mascot, main, stats, actions, motto;
  bool landscape;
};
inline Home home(fui::Rect body, int titleLine, int bodyLine, int smallLine) {
  Home result{};
  result.landscape = body.width > body.height;
  result.motto = box(body.x, body.bottom() - smallLine, body.width, smallLine);
  body.height = std::max(0, body.height - smallLine - gap);
  const int profileHeight = titleLine + smallLine + (result.landscape ? 4 : bodyLine + 8);
  const int statsHeight = smallLine + titleLine + padding * 2;
  constexpr int dogWidth = 111, dogHeight = 90;
  if (result.landscape) {
    const int width = (body.width - gap) / 2;
    auto left = box(body.x, body.y, width, body.height);
    auto right = box(body.right() - width, body.y, width, body.height);
    result.profile = top(right, profileHeight);
    result.main = box(left.x, left.y + dogHeight, left.width, left.height - dogHeight);
    result.stats = box(right.x, right.y + (right.height - statsHeight) / 2, right.width, statsHeight);
    result.actions = right;
  } else {
    result.profile = top(body, profileHeight);
    result.stats = box(body.x, body.bottom() - statsHeight, body.width, statsHeight);
    result.actions = result.stats;
    const int available = result.stats.y - gap - body.y;
    const int cardHeight = std::min(264, available - dogHeight);
    result.main = box(body.x, result.stats.y - gap - cardHeight, body.width, cardHeight);
  }
  // The paws meet the card edge; the rest of the illustration has its own space.
  result.mascot = box(result.main.right() - padding - dogWidth, result.main.y - dogHeight, dogWidth, dogHeight);
  return result;
}
struct Settings {
  fui::Rect title, profile, grid, about, help;
  int columns;
};
inline Settings settings(fui::Rect body, int titleLine, int bodyLine, int smallLine) {
  Settings result{};
  result.columns = body.width > body.height ? 3 : 2;
  result.title = top(body, titleLine + 8, 8);
  result.profile = top(body, std::max(80, bodyLine + smallLine + 16));
  const int footerHeight = bodyLine + 20;
  const int footerRows = result.columns == 3 ? 1 : 2;
  const int footerSpace = footerHeight * footerRows + (footerRows - 1) * 8;
  result.grid = top(body, std::max(0, body.height - footerSpace - gap));
  if (footerRows == 1) {
    const int width = (body.width - gap) / 2;
    result.about = box(body.x, body.y, width, body.height);
    result.help = box(body.right() - width, body.y, width, body.height);
  } else {
    result.about = top(body, footerHeight, 8);
    result.help = body;
  }
  return result;
}
}  // namespace RickyCompactLayout
