#pragma once

#include <algorithm>

// Tap rect of the back button BaseTheme::drawHeader paints on touch boards.
// The header's FreeInkUI frame is non-interactive, so the rect is recorded
// here at draw time and consumed by MappedInputManager's Back mapping; every
// screen that draws a titled header gets tap-to-go-back without its own
// routing. Cleared on activity exit (and by headerless draws) so a stale rect
// never eats taps on a new screen. Written by the render task and read by the
// loop task; a torn int read at worst misroutes one tap on a band that is
// being redrawn, so no lock is taken.
namespace HeaderBackTapTarget {
inline int x = 0;
inline int y = 0;
inline int w = 0;
inline int h = 0;

inline void set(const int newX, const int newY, const int newW, const int newH) {
  x = newX;
  y = newY;
  w = newW;
  h = newH;
}

// Back owns the leading half of the header, including the blank left gutter.
// Safe-area/page insets must not create a dead strip beside the arrow. Use the
// full band height and a width independent of the translated title's length;
// the trailing half stays available to Home and other header actions. Never
// extend below the band into the first content row.
inline void setLeading(const int bandX, const int bandY, const int bandWidth, const int bandHeight) {
  const int left = std::min(0, bandX);
  const int right = bandX + std::max(0, bandWidth / 2);
  set(left, bandY, bandWidth > 0 ? std::max(0, right - left) : 0, std::max(0, bandHeight));
}

inline void clear() { w = 0; }

inline bool contains(const int tx, const int ty) { return w > 0 && tx >= x && tx < x + w && ty >= y && ty < y + h; }
}  // namespace HeaderBackTapTarget
