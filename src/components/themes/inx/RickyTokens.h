#pragma once

#include <cstdint>

// RickyOS 1.2 design tokens (docs/rickyos-1.2-plan.md, section 3.2). Pages and InxTheme
// metrics take their sizes from here instead of local numbers. Panel: 684x1216, ~297 ppi.
namespace ricky::tokens {

// Type scale, in pixels of body height.
constexpr int kTypeDisplay = 48;
constexpr int kTypeTitle = 33;
constexpr int kTypeHeadline = 29;
constexpr int kTypeBody = 25;
constexpr int kTypeCaption = 21;

// Spacing: an 8 px grid, plus the page margin and gutter.
constexpr int kGrid = 8;
constexpr int kPageMargin = 40;
constexpr int kGutter = 20;
constexpr int kContentWidth = 684 - 2 * kPageMargin;  // 604

// Corners: cards, and controls (rows, buttons, fields, popups). Chips and tabs are pills.
constexpr int kCardRadius = 24;
constexpr int kControlRadius = 12;
constexpr int kTabPillWidth = 88;
constexpr int kTabPillHeight = 48;

// The only two line widths.
constexpr int kLineHairline = 2;
constexpr int kLineStrong = 4;

// The only six grays, 0 = black ... 15 = white. In a fast B/W frame they threshold:
// ink and ink-2 draw black; line, tint and paper draw white. The gray settle shows them.
constexpr uint8_t kInk = 0;
constexpr uint8_t kInk2 = 4;
constexpr uint8_t kMute = 8;
constexpr uint8_t kLine = 11;
constexpr uint8_t kTint = 13;
constexpr uint8_t kPaper = 15;

// Selection is always an invert: the carrier turns ink, its content turns paper.

}  // namespace ricky::tokens
