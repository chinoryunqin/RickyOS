#pragma once
#ifdef RICKYOS_PRODUCT
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

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

// Quiet secondary affordance for rows that open a list or page.
inline void chevron(fui::DrawTarget& target, fui::Rect r) {
  const auto black = fui::Paint::solid(fui::Color::Black);
  const int16_t mid = r.y + r.height / 2;
  const int16_t tip = r.right() - 3;
  target.line({static_cast<int16_t>(tip - 7), static_cast<int16_t>(mid - 8)}, {tip, mid}, 2, black);
  target.line({tip, mid}, {static_cast<int16_t>(tip - 7), static_cast<int16_t>(mid + 8)}, 2, black);
}

// The UI faces ship one CJK weight; headings and book titles use the renderer's
// synthetic bold while this guard is alive. Text without style.bold is unaffected.
using Bold = GfxRenderer::SyntheticBoldScope;

inline void progressBar(fui::DrawTarget& target, fui::Rect r, int percent) {
  target.fill(r, fui::Paint::dither(fui::Color::LightGray));
  const int filled = r.width * std::clamp(percent, 0, 100) / 100;
  if (filled > 0)
    target.fill(fui::Rect{r.x, r.y, static_cast<int16_t>(filled), r.height}, fui::Paint::solid(fui::Color::Black));
}

// Generated covers pick one small line motif from the title so a shelf of
// cover-less books stays distinguishable. Deterministic, no assets or heap.
enum class Motif : uint8_t { Tree, Star, Flower, Leaf, Mountain, Wave, Count };
inline Motif motifFor(const char* text) {
  uint32_t hash = 2166136261u;
  for (const char* p = text ? text : ""; *p; ++p) hash = (hash ^ static_cast<uint8_t>(*p)) * 16777619u;
  return static_cast<Motif>(hash % static_cast<uint32_t>(Motif::Count));
}
inline void motif(fui::DrawTarget& target, Rect r, Motif kind) {
  const auto black = fui::Paint::solid(fui::Color::Black);
  const int x = r.x, y = r.y, s = r.width;
  const auto at = [&](int px, int py) {
    return fui::Point{static_cast<int16_t>(x + px * s / 24), static_cast<int16_t>(y + py * s / 24)};
  };
  const auto path = [&](const int (*points)[2], int count, bool closed) {
    for (int i = 0; i + 1 < count; ++i)
      target.line(at(points[i][0], points[i][1]), at(points[i + 1][0], points[i + 1][1]), 2, black);
    if (closed) target.line(at(points[count - 1][0], points[count - 1][1]), at(points[0][0], points[0][1]), 2, black);
  };
  const auto circle = [&](int cx, int cy, int radius) {
    const auto rect =
        uiRect(Rect{x + (cx - radius) * s / 24, y + (cy - radius) * s / 24, radius * 2 * s / 24, radius * 2 * s / 24});
    target.stroke(rect, black, 2, static_cast<uint8_t>(rect.width / 2));
  };
  switch (kind) {
    case Motif::Tree: {
      circle(12, 7, 5);
      circle(7, 12, 4);
      circle(17, 12, 4);
      static constexpr int trunk[][2] = {{12, 12}, {12, 22}};
      static constexpr int ground[][2] = {{7, 22}, {17, 22}};
      path(trunk, 2, false);
      path(ground, 2, false);
      break;
    }
    case Motif::Star: {
      static constexpr int star[][2] = {{12, 2},  {14, 9}, {22, 9}, {16, 14}, {18, 21},
                                        {12, 17}, {6, 21}, {8, 14}, {2, 9},   {10, 9}};
      path(star, 10, true);
      break;
    }
    case Motif::Flower: {
      circle(12, 5, 3);
      circle(17, 10, 3);
      circle(7, 10, 3);
      circle(12, 15, 3);
      static constexpr int stem[][2] = {{12, 18}, {12, 23}};
      static constexpr int leaf[][2] = {{12, 21}, {16, 19}};
      path(stem, 2, false);
      path(leaf, 2, false);
      break;
    }
    case Motif::Leaf: {
      static constexpr int outline[][2] = {{4, 20},  {4, 13},  {7, 8},   {12, 5}, {20, 4},
                                           {19, 12}, {16, 17}, {11, 20}, {4, 20}};
      static constexpr int rib[][2] = {{4, 20}, {15, 9}};
      static constexpr int stem[][2] = {{2, 22}, {4, 20}};
      path(outline, 9, false);
      path(rib, 2, false);
      path(stem, 2, false);
      break;
    }
    case Motif::Mountain: {
      static constexpr int ridge[][2] = {{2, 21}, {9, 9}, {13, 15}, {16, 11}, {22, 21}};
      static constexpr int base[][2] = {{2, 21}, {22, 21}};
      path(ridge, 5, false);
      path(base, 2, false);
      circle(18, 5, 2);
      break;
    }
    case Motif::Wave:
    case Motif::Count: {
      static constexpr int upper[][2] = {{2, 10}, {5, 7}, {8, 10}, {11, 13}, {14, 10}, {17, 7}, {20, 10}, {22, 12}};
      static constexpr int lower[][2] = {{2, 16}, {5, 13}, {8, 16}, {11, 19}, {14, 16}, {17, 13}, {20, 16}, {22, 18}};
      path(upper, 8, false);
      path(lower, 8, false);
      break;
    }
  }
}

// Index titles fall back to the file name; show "浮生六记", not "浮生六记.txt".
inline void stripBookExtension(std::string& title) {
  static constexpr const char* extensions[] = {".epub", ".txt", ".md", ".xtch", ".xtc"};
  for (const char* extension : extensions) {
    const size_t length = strlen(extension);
    if (title.size() <= length) continue;
    bool match = true;
    for (size_t i = 0; i < length && match; ++i)
      match = tolower(static_cast<unsigned char>(title[title.size() - length + i])) == extension[i];
    if (match) {
      title.resize(title.size() - length);
      return;
    }
  }
}

// Covers carry the family name, like a spine: the part after the last
// middle dot ("亨利·戴维·梭罗" -> "梭罗"). Returns the whole name otherwise.
inline const char* coverAuthor(const char* author) {
  if (!author) return nullptr;
  const char* tail = author;
  for (const char* p = author; *p; ++p) {
    const auto* u = reinterpret_cast<const unsigned char*>(p);
    if (u[0] == 0xC2 && u[1] == 0xB7)
      tail = p + 2;  // U+00B7
    else if (u[0] == 0xE3 && u[1] == 0x83 && u[2] == 0xBB)
      tail = p + 3;  // U+30FB
    else if (u[0] == 0xE2 && u[1] == 0x80 && (u[2] == 0xA2 || u[2] == 0xA7))
      tail = p + 3;  // U+2022/2027
  }
  return *tail ? tail : author;
}

// Typographic cover for books without artwork: double rule, author, title,
// motif and an optional footer (format). `text` is a full-coverage face; the
// title is drawn bold, so callers keep a Bold guard alive.
inline void generatedCover(fui::DrawTarget& target, fui::Rect rect, const char* title, const char* author,
                           const char* footer, fui::TextStyle text) {
  if (rect.empty()) return;
  const auto black = fui::Paint::solid(fui::Color::Black);
  target.fill(rect, fui::Paint::solid(fui::Color::White));
  target.stroke(rect, black, 1);
  const int16_t rule = std::max<int16_t>(5, rect.width / 22);
  if (rect.width > rule * 4 && rect.height > rule * 4)
    target.stroke(fui::Rect{static_cast<int16_t>(rect.x + rule), static_cast<int16_t>(rect.y + rule),
                            static_cast<int16_t>(rect.width - rule * 2), static_cast<int16_t>(rect.height - rule * 2)},
                  black, 1);
  const int lineHeight = target.lineHeight(text.font);
  const int pad = rule + std::max(6, rect.width / 14);
  const int inner = rect.width - pad * 2;
  if (lineHeight <= 0 || inner <= 0) return;
  text.align = fui::TextAlign::Center;
  text.maxLines = 1;
  const bool roomy = rect.height >= lineHeight * 6;
  int top = rect.y + pad, bottom = rect.bottom() - pad;
  author = coverAuthor(author);
  if (roomy && author && *author && target.measureText(text.font, author, text).width <= inner) {
    target.text(fui::Rect{static_cast<int16_t>(rect.x + pad), static_cast<int16_t>(top), static_cast<int16_t>(inner),
                          static_cast<int16_t>(lineHeight)},
                author, text);
    top += lineHeight;
  }
  if (roomy && footer && *footer) {
    bottom -= lineHeight;
    target.text(fui::Rect{static_cast<int16_t>(rect.x + pad), static_cast<int16_t>(bottom), static_cast<int16_t>(inner),
                          static_cast<int16_t>(lineHeight)},
                footer, text);
  }
  auto heading = text;
  heading.bold = true;
  heading.maxLines = std::clamp((bottom - top) / lineHeight - 1, 1, 3);
  const int titleHeight = fui::measureWrappedText(target, title, heading, inner).height;
  const int size = std::clamp(rect.width / 4, 18, 40);
  const int gap = std::max(6, lineHeight / 3);
  const bool drawMotif = bottom - top >= titleHeight + gap + size;
  const int group = titleHeight + (drawMotif ? gap + size : 0);
  const int y = top + std::max(0, (bottom - top - group) / 2);
  wrappedText(target,
              fui::Rect{static_cast<int16_t>(rect.x + pad), static_cast<int16_t>(y), static_cast<int16_t>(inner),
                        static_cast<int16_t>(titleHeight)},
              title, heading);
  if (drawMotif)
    motif(target, Rect{rect.x + (rect.width - size) / 2, y + titleHeight + gap, size, size}, motifFor(title));
}

// Landscape "recently opened" tile: motif + one-line title inside a hairline frame.
inline void bookTile(fui::DrawTarget& target, fui::Rect rect, const char* title, fui::TextStyle text,
                     Motif kind = Motif::Count) {
  if (rect.empty()) return;
  target.fill(rect, fui::Paint::solid(fui::Color::White));
  target.stroke(rect, fui::Paint::solid(fui::Color::Black), 1);
  const int lineHeight = target.lineHeight(text.font);
  const int size = std::min<int>(lineHeight, rect.height / 2);
  const int gap = std::max(8, lineHeight / 3);
  const int maxText = std::max(0, rect.width - size - gap - gap * 4);
  text.maxLines = 1;
  text.align = fui::TextAlign::Left;
  const int textWidth = std::min<int>(maxText, target.measureText(text.font, title, text).width);
  const int x = rect.x + std::max(gap * 2, (rect.width - size - gap - textWidth) / 2);
  motif(target, Rect{x, rect.y + (rect.height - size) / 2, size, size}, kind == Motif::Count ? motifFor(title) : kind);
  target.text(
      fui::Rect{static_cast<int16_t>(x + size + gap), static_cast<int16_t>(rect.y + (rect.height - lineHeight) / 2),
                static_cast<int16_t>(maxText), static_cast<int16_t>(lineHeight)},
      title, text);
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
