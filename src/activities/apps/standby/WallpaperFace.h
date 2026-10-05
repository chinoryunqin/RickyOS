#pragma once
#ifdef RICKYOS_PRODUCT

#include <cstdint>

#include "StandbyFace.h"

// RickyOS Standby: the user's own picture (the one chosen under Settings → Power &
// Standby, stored as /sleep.bmp) in native 16-gray, with an optional date or
// time-and-date corner (CrossPointSettings::standbyOverlay).
//
// The picture is drawn once through the 16-gray frame. After that, a minute tick
// redraws only the corner in the B/W proxy and pushes it with a partial refresh:
// unchanged pixels get no drive, so the picture keeps its grays and the screen does
// not flash. On the hour the whole frame is redrawn to clear the corner's ghosting.
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

 private:
  void drawCorner(GfxRenderer& renderer, const Rect& viewport, bool intoGray) const;

  bool hasPicture_ = false;
  bool pictureShown_ = false;  // the 16-gray frame is on the panel; minute ticks update the corner only
  bool fullRedraw_ = true;     // next render redraws the picture too
  int32_t lastMinute_ = -1;    // local minute of day last drawn, -1 before the first draw
  int32_t lastDay_ = -1;       // local day of year last drawn
};

#endif
