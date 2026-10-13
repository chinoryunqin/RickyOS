#pragma once

#include <cstdint>

#include "util/ReadingStreak.h"

// The dog's paw print: the stamp the reading streak and the month calendar draw for a day.
// Drawn from geometry (a main pad and four toes), so one shape serves every size, in plain
// ink that reads the same in B/W and 16-level frames:
//
//   Goal   solid           the daily goal met
//   Read   outlined        read, under the goal
//   Rest   hatched         a rest card kept the streak
//   None   dotted outline  a print still to make: today before reading, or a missed day
namespace rickyPaw {

enum class Part : uint8_t { Outside, Pad, Toe };

// Which part of a `size` x `size` paw the pixel (x, y) falls in. Fractions of the size,
// scaled by 100 to stay in integers.
constexpr Part partAt(const int x, const int y, const int size) {
  if (size <= 0 || x < 0 || y < 0 || x >= size || y >= size) return Part::Outside;
  const int px = x * 100 + 50;  // pixel centre, in hundredths of a pixel
  const int py = y * 100 + 50;
  const int s = size;
  // Main pad: an ellipse low in the box.
  const int cx = 50 * s, cy = 68 * s, rx = 30 * s, ry = 24 * s;
  const long long dx = px - cx, dy = py - cy;
  if (dx * dx * ry * ry + dy * dy * rx * rx <= 1LL * rx * rx * ry * ry) return Part::Pad;
  // Toes: four discs fanned over the pad.
  constexpr int kToes[4][2] = {{17, 40}, {35, 17}, {65, 17}, {83, 40}};
  const int r = 12 * s;
  for (const auto& toe : kToes) {
    const long long tx = px - toe[0] * s, ty = py - toe[1] * s;
    if (tx * tx + ty * ty <= 1LL * r * r) return Part::Toe;
  }
  return Part::Outside;
}

// On the outline: inside the shape with an outside pixel within `stroke` pixels.
constexpr bool onEdge(const int x, const int y, const int size, const int stroke) {
  if (partAt(x, y, size) == Part::Outside) return false;
  for (int d = 1; d <= stroke; ++d) {
    if (partAt(x - d, y, size) == Part::Outside || partAt(x + d, y, size) == Part::Outside ||
        partAt(x, y - d, size) == Part::Outside || partAt(x, y + d, size) == Part::Outside) {
      return true;
    }
  }
  return false;
}

// Whether the pixel is ink for a paw in `mark`'s look.
constexpr bool inkAt(const int x, const int y, const int size, const readingStreak::Mark mark) {
  const Part part = partAt(x, y, size);
  if (part == Part::Outside) return false;
  const int stroke = size >= 40 ? 3 : 2;
  switch (mark) {
    case readingStreak::Mark::Goal:
      return true;
    case readingStreak::Mark::Read:
      return onEdge(x, y, size, stroke);
    case readingStreak::Mark::Rest:
      return onEdge(x, y, size, stroke) || (x + y) % 4 == 0;
    case readingStreak::Mark::None:
      break;
  }
  return onEdge(x, y, size, 1) && (x + y) % 2 == 0;
}

template <typename Renderer>
void draw(const Renderer& renderer, const int originX, const int originY, const int size,
          const readingStreak::Mark mark) {
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (inkAt(x, y, size, mark)) renderer.drawPixel(originX + x, originY + y, true);
    }
  }
}

}  // namespace rickyPaw
