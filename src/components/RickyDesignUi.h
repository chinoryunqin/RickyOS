#pragma once

#include <GfxRenderer.h>
#include <Utf8.h>

#include <cstring>

#include "RickyDesignTokens.h"
#include "RickyPageLayout.h"
#include "UiAppHost.h"

// Product-only immutable font IDs, independent of generated reader font IDs.
namespace RickyDesignUi {
namespace ds = ricky::ds;
namespace fui = freeink::ui;
inline constexpr int BODY = 0x524401;
inline constexpr int CAPTION = 0x524402;
inline constexpr int HEADLINE = 0x524403;
inline constexpr int TITLE = 0x524404;
inline constexpr int NUMERIC_DISPLAY = 0x524405;

// Whole-run selection: never silently mix bold/regular glyphs in a title.
inline int fontFor(const GfxRenderer& renderer, int preferred, const char* text) {
  if (!text || preferred == BODY) return preferred;
  const auto it = renderer.getFontMap().find(preferred);
  if (it == renderer.getFontMap().end()) return BODY;
  const uint8_t* cursor = reinterpret_cast<const uint8_t*>(text);
  for (uint32_t cp; (cp = utf8NextCodepoint(&cursor)) != 0;)
    if (!it->second.hasCodepoint(cp)) return BODY;
  return preferred;
}

// Exact native tones; B/W path uses the spec fallback, never Bayer dots.
inline void fill(const GfxRenderer& renderer, fui::Rect r, ds::gray::Level tone, int radius = 0) {
  if (r.empty()) return;
  if (!renderer.isGrayscale16Active() || tone == ds::gray::kInk || tone == ds::gray::kPaper) {
    renderer.fillRoundedRect(r.x, r.y, r.width, r.height, radius,
                             ds::gray::oneBitInk(tone) ? Color::Black : Color::White);
    return;
  }
  radius = std::min({radius, r.width / 2, r.height / 2});
  // No frame/scratch allocation. Restricted to small UI fills and thin tracks.
  for (int x = 0; x < r.width; ++x)
    for (int y = 0; y < r.height; ++y) {
      const int dx = x < radius ? radius - x : x >= r.width - radius ? x - (r.width - radius - 1) : 0;
      const int dy = y < radius ? radius - y : y >= r.height - radius ? y - (r.height - radius - 1) : 0;
      if (dx && dy && dx * dx + dy * dy > radius * radius) continue;
      renderer.drawGrayscale16Pixel(r.x + x, r.y + y, ds::gray::toRgb8(tone));
    }
}

inline void card(const GfxRenderer& renderer, fui::Rect r, bool selected) {
  fill(renderer, r, selected ? ds::gray::kInk : ds::gray::kPaper, ds::radius::kContainer);
  if (!selected)
    renderer.drawRoundedRect(r.x, r.y, r.width, r.height, ds::stroke::kHairline, ds::radius::kContainer, true);
}

inline void progress(const GfxRenderer& renderer, fui::Rect r, int percent, bool inverted = false) {
  const auto ink = inverted ? ds::gray::kPaper : ds::gray::kInk;
  if (renderer.isGrayscale16Active() && !inverted)
    fill(renderer, r, ds::gray::kTint, r.height / 2);
  else
    fill(renderer,
         {r.x, static_cast<int16_t>(r.y + (r.height - ds::stroke::kHairline) / 2), r.width, ds::stroke::kHairline},
         ink);
  const int width = r.width * std::clamp(percent, 0, 100) / 100;
  if (width) fill(renderer, {r.x, r.y, static_cast<int16_t>(width), r.height}, ink, r.height / 2);
}

inline void text(const GfxRenderer& renderer, fui::Rect r, const char* value, int font = BODY, bool inverted = false,
                 fui::TextAlign align = fui::TextAlign::Left, int lines = 1) {
  // A tiny stack adapter avoids changing the host's published font bindings.
  // Layout's fixed-capacity UTF-8 wrapping avoids per-line heap vectors.
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, fontFor(renderer, font, value));
  fui::TextStyle style;
  style.font = fui::GfxRendererTarget::FONT_BODY;
  style.color = inverted ? fui::Color::White : fui::Color::Black;
  style.align = align;
  style.maxLines = lines;
  const int height = target.lineHeight(style.font);
  if (height <= 0 || r.empty()) return;
  const GfxRenderer::ClipScope clip(renderer, r.x, r.y, r.width, r.height);
  if (r.height < height) {
    style.maxLines = 1;
    target.text(r, value, style);
    return;
  }
  style.maxLines = std::min<int>(style.maxLines, r.height / height);
  fui::layoutText(target, r, value, style, [&](const char* run, fui::Rect bounds) {
    auto single = style;
    single.maxLines = 1;
    single.align = fui::TextAlign::Left;
    target.text(bounds, run, single);
  });
}

inline void cover(const GfxRenderer& renderer, fui::Rect r, const char* title, const char* format) {
  if (r.empty()) return;
  const GfxRenderer::ClipScope clip(renderer, r.x, r.y, r.width, r.height);
  const int style = ds::component::coverTemplateFor(title);
  const bool dark = style == 0 || style == 3;
  fill(renderer, r, dark ? ds::gray::kInk : style == 4 ? ds::gray::kTint : ds::gray::kPaper);
  renderer.drawRect(r.x, r.y, r.width, r.height, ds::stroke::kHairline, !dark);
  const int pad = ds::space::k4;
  if (style == 2) {
    const int band = std::min<int>(ds::space::k12, r.height / 4);
    fill(renderer, {r.x, static_cast<int16_t>(r.bottom() - band), r.width, static_cast<int16_t>(band)}, ds::gray::kInk);
  } else if (style == 5) {
    renderer.drawRect(r.x + pad / 2, r.y + pad / 2, r.width - pad, r.height - pad, ds::stroke::kHairline, true);
  } else if (style == 4) {
    // Thin, widely-spaced rules rather than a checkerboard texture. Paper in
    // interaction frames; exact named line gray in resting frames.
    if (renderer.isGrayscale16Active())
      for (int y = r.bottom() - 64; y < r.bottom() - 40; y += 8)
        fill(renderer,
             {static_cast<int16_t>(r.x + 8), static_cast<int16_t>(y), static_cast<int16_t>(r.width - 16),
              ds::stroke::kHairline},
             ds::gray::kLine);
  }
  const int font = fontFor(renderer, HEADLINE, title);
  const int lh = renderer.getLineHeight(font);
  int chars = 0;
  const uint8_t* cursor = reinterpret_cast<const uint8_t*>(title ? title : "");
  bool cjk = true;
  for (uint32_t cp; (cp = utf8NextCodepoint(&cursor)) != 0;) {
    ++chars;
    cjk &= cp >= 0x3000;
  }
  const bool vertical = r.width >= 160 && chars > 0 && chars <= 5 && cjk && chars * lh <= r.height - 64;
  const fui::Rect label{static_cast<int16_t>(r.x + pad), static_cast<int16_t>(r.y + pad),
                        static_cast<int16_t>(std::max(1, r.width - pad * 2)),
                        static_cast<int16_t>(std::max(1, r.height - pad * 2 - 32))};
  if (vertical) {
    cursor = reinterpret_cast<const uint8_t*>(title);
    int y = label.y + std::max(0, (label.height - chars * lh) / 2);
    while (*cursor) {
      const uint8_t* begin = cursor;
      utf8NextCodepoint(&cursor);
      char glyph[5]{};
      const size_t bytes = std::min<size_t>(4, cursor - begin);
      memcpy(glyph, begin, bytes);
      text(renderer, {label.x, static_cast<int16_t>(y), label.width, static_cast<int16_t>(lh)}, glyph, font, dark,
           fui::TextAlign::Center);
      y += lh;
    }
  } else {
    const int rows = std::min(3, label.height / std::max(1, lh));
    auto centered = label;
    centered.y += std::max(0, (label.height - rows * lh) / 2);
    centered.height = rows * lh;
    text(renderer, centered, title, font, dark, fui::TextAlign::Center, rows);
  }
  text(renderer,
       {static_cast<int16_t>(r.x + pad), static_cast<int16_t>(r.bottom() - pad - 28),
        static_cast<int16_t>(r.width - pad * 2), 28},
       format ? format : "", CAPTION, dark || style == 2);
}
}  // namespace RickyDesignUi
