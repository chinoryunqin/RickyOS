#pragma once
#ifdef RICKYOS_PRODUCT

#include <cstdint>

#include "StandbyFace.h"

// RickyOS Standby: the user's own picture (the one chosen under Settings → Power &
// Standby, stored as /sleep.bmp) in native 16-gray, with an optional date or
// time-and-date corner (CrossPointSettings::standbyOverlay).
//
// Each update (the first draw, then every minute for the time or every day for the
// date) redraws the whole picture through the 16-gray frame with the corner on top:
// this panel has no partial refresh, and a B/W refresh would flatten the grays.
class WallpaperFace final : public StandbyFace {
 public:
  void onEnter() override;
  TickResult tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  bool renderNative(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override { return StrId::STR_STANDBY_TITLE; }
  uint32_t secondsUntilNextWake() const override;
  bool wantsClock() const override;
  bool needsPicture() const override { return !hasPicture_; }
  PictureAction pictureActionAt(int x, int y) const override;

 private:
  void drawCorner(GfxRenderer& renderer, const Rect& viewport, bool intoGray) const;

  bool hasPicture_ = false;
  int32_t lastMinute_ = -1;  // local minute of day last drawn, -1 before the first draw
  // Empty-state buttons as last drawn, for tap hit-testing.
  Rect chooseButton_{};
  Rect downloadButton_{};
  int32_t lastDay_ = -1;  // local day of year last drawn
};

#endif
