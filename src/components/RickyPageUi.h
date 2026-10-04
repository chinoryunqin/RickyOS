#pragma once
#ifdef RICKYOS_PRODUCT
#include "RickyPageLayout.h"
#include "UiAppHost.h"
#include "components/lists/tile-grid.h"

namespace RickyPageUi {
namespace fui = freeink::ui;
enum class Icon { Book, Font, Image, Download, Display, Network, Power, System, Drive, Folder, Upload };

inline fui::Rect uiRect(const Rect& r) {
  return {static_cast<int16_t>(r.x), static_cast<int16_t>(r.y), static_cast<int16_t>(r.width),
          static_cast<int16_t>(r.height)};
}
// Native wrappedText truncates a CJK run with no spaces as a single word.
// Use the SDK's bounded UTF-8 character wrapping, then emit single-line runs.
// This also makes drawing agree with measureWrappedText, without a wrap vector.
inline void wrappedText(fui::DrawTarget& target, fui::Rect rect, const char* text, fui::TextStyle style) {
  const int lineHeight = target.lineHeight(style.font);
  if (rect.empty() || lineHeight <= 0 || rect.height < lineHeight) return;
  style.maxLines = std::clamp<int>(style.maxLines, 1, rect.height / lineHeight);
  fui::layoutText(target, rect, text, style, [&](const char* line, fui::Rect bounds) {
    bounds.width = std::min<int>(bounds.width, rect.right() - bounds.x);
    auto run = style;
    run.maxLines = 1;
    run.align = fui::TextAlign::Left;  // layoutText has already positioned this run.
    target.text(bounds, line, run);
  });
}
inline void arrow(fui::DrawTarget& target, fui::Rect r) {
  const auto black = fui::Paint::solid(fui::Color::Black);
  const int16_t mid = r.y + r.height / 2;
  const int16_t end = r.right() - 2;
  target.line({r.x, mid}, {end, mid}, 2, black);
  target.line({static_cast<int16_t>(end - 7), static_cast<int16_t>(mid - 7)}, {end, mid}, 2, black);
  target.line({static_cast<int16_t>(end - 7), static_cast<int16_t>(mid + 7)}, {end, mid}, 2, black);
}

// Small original 1-bit line icons: reuse the render target, no image or heap cache.
inline void icon(fui::DrawTarget& target, Rect r, Icon kind) {
  const auto black = fui::Paint::solid(fui::Color::Black);
  const int x = r.x, y = r.y, s = r.width;
  const auto line = [&](int x1, int y1, int x2, int y2) {
    target.line({static_cast<int16_t>(x + x1 * s / 24), static_cast<int16_t>(y + y1 * s / 24)},
                {static_cast<int16_t>(x + x2 * s / 24), static_cast<int16_t>(y + y2 * s / 24)}, 2, black);
  };
  const auto box = [&](int bx, int by, int w, int h, int radius = 0) {
    target.stroke(uiRect(Rect{x + bx * s / 24, y + by * s / 24, w * s / 24, h * s / 24}), black, 2, radius);
  };
  switch (kind) {
    case Icon::Book:
      box(2, 4, 10, 16, 2);
      box(12, 4, 10, 16, 2);
      line(5, 8, 9, 8);
      line(15, 8, 19, 8);
      break;
    case Icon::Font:
      line(4, 4, 20, 4);
      line(12, 4, 12, 21);
      line(8, 21, 16, 21);
      break;
    case Icon::Image:
      box(2, 3, 20, 18, 2);
      box(6, 6, 3, 3, 2);
      line(4, 18, 11, 11);
      line(11, 11, 20, 18);
      break;
    case Icon::Download:
      line(12, 2, 12, 15);
      line(7, 10, 12, 15);
      line(12, 15, 17, 10);
      line(3, 17, 3, 21);
      line(3, 21, 21, 21);
      line(21, 21, 21, 17);
      break;
    case Icon::Display:
      box(2, 3, 20, 14, 2);
      line(12, 17, 12, 21);
      line(7, 21, 17, 21);
      break;
    case Icon::Network:
      line(3, 7, 7, 4);
      line(7, 4, 17, 4);
      line(17, 4, 21, 7);
      line(6, 12, 9, 10);
      line(9, 10, 15, 10);
      line(15, 10, 18, 12);
      line(9, 17, 15, 17);
      box(11, 21, 2, 2);
      break;
    case Icon::Power:
      box(3, 6, 18, 17, s / 3);
      line(12, 1, 12, 12);
      break;
    case Icon::System:
      line(2, 5, 22, 5);
      line(2, 12, 22, 12);
      line(2, 19, 22, 19);
      box(6, 3, 3, 4, 2);
      box(15, 10, 3, 4, 2);
      box(8, 17, 3, 4, 2);
      break;
    case Icon::Drive:
      line(3, 14, 6, 6);
      line(6, 6, 18, 6);
      line(18, 6, 21, 14);
      box(3, 14, 18, 6, 2);
      line(16, 17, 18, 17);
      break;
    case Icon::Folder:
      line(2, 8, 2, 20);
      line(2, 8, 2, 5);
      line(2, 5, 9, 5);
      line(9, 5, 12, 8);
      line(12, 8, 22, 8);
      line(22, 8, 22, 20);
      line(22, 20, 2, 20);
      break;
    case Icon::Upload:
      line(12, 15, 12, 2);
      line(7, 7, 12, 2);
      line(12, 2, 17, 7);
      line(3, 14, 3, 21);
      line(3, 21, 21, 21);
      line(21, 21, 21, 14);
      break;
  }
}

inline int syncNav(fui::ListNav& nav, int count) {
  fui::ListProps paging;
  nav.syncToProps(fui::Rect{0, 0, 1, static_cast<int16_t>(count)}, 1, 0, count, paging, 0);
  nav.drawnCount = count;
  nav.onListRendered(0, count, paging.selectedIndex >= 0 && paging.selectedIndex < count);
  return paging.selectedIndex;
}

inline void bookPlaceholder(fui::DrawTarget& target, fui::Rect rect, const char* title, fui::TextStyle text) {
  if (rect.empty()) return;
  target.fill(rect, fui::Paint::solid(fui::Color::White));
  target.stroke(rect, fui::Paint::solid(fui::Color::Black), 1);
  const int gap = std::max<int>(6, std::min(rect.width, rect.height) / 12);
  const int lineHeight = target.lineHeight(text.font);
  if (lineHeight <= 0 || rect.height < lineHeight + gap * 2 || rect.width <= gap * 2) return;
  const int size = std::max(0, std::min({36, rect.width - gap * 2, rect.height - lineHeight - gap * 3}));
  const int lines = std::clamp((rect.height - size - gap * 3) / lineHeight, 1, 3);
  const int top = rect.y + (rect.height - size - gap - lines * lineHeight) / 2;
  if (size > 0) icon(target, Rect{rect.x + (rect.width - size) / 2, top, size, size}, Icon::Book);
  text.align = fui::TextAlign::Center;
  text.maxLines = lines;
  wrappedText(target,
              fui::Rect{static_cast<int16_t>(rect.x + gap), static_cast<int16_t>(top + size + gap),
                        static_cast<int16_t>(rect.width - gap * 2), static_cast<int16_t>(lines * lineHeight)},
              title, text);
}

inline void tile(UiAppHost::UiScreen& screen, Rect cell, const char* label, const char* detail, Icon kind,
                 fui::ActionId action, int value, bool selected) {
  if (cell.width <= 0 || cell.height <= 0) return;
  const auto& theme = screen.theme();
  const int gap = std::max<int>(6, theme.spaceSm);
  const auto rect = uiRect(cell);
  screen.frame().hit(rect, action, value, fui::InputTouch);
  const auto state = screen.frame().stateFor(action, value, selected ? fui::StateSelected : fui::StateNormal);
  screen.target().stroke(rect, fui::Paint::solid(fui::Color::Black), state == fui::StateNormal ? 1 : 3,
                         theme.controlRadius);
  const int lineHeight = screen.target().lineHeight(theme.bodyText.font);
  const int smallHeight = detail ? screen.target().lineHeight(theme.smallText.font) : 0;
  const int size = std::max(
      0, std::min({lineHeight * 3 / 2, cell.width - gap * 2, cell.height - lineHeight - smallHeight - gap * 4}));
  const int top = cell.y + std::max(gap, (cell.height - size - lineHeight - smallHeight - gap * 2) / 2);
  if (size > 0) icon(screen.target(), Rect{cell.x + (cell.width - size) / 2, top, size, size}, kind);
  auto title = theme.bodyText;
  title.align = fui::TextAlign::Center;
  title.maxLines = 1;
  screen.target().text(uiRect(Rect{cell.x + gap, top + size + gap, cell.width - gap * 2, lineHeight}), label, title);
  if (detail) {
    auto small = theme.smallText;
    small.align = fui::TextAlign::Center;
    small.maxLines = 1;
    screen.target().text(
        uiRect(Rect{cell.x + gap, top + size + gap * 2 + lineHeight, cell.width - gap * 2, smallHeight}), detail,
        small);
  }
}
}  // namespace RickyPageUi
#endif
