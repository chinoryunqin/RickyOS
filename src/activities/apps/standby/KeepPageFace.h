#pragma once
#ifdef RICKYOS_PRODUCT

#include "StandbyFace.h"

// RickyOS Standby that leaves the page as it was: the page under Standby is still in
// the frame buffer, so only a small "Standby" label is added and pushed as a quiet
// difference. Nothing ticks, so the light sleep between wakes is as long as it gets.
class KeepPageFace final : public StandbyFace {
 public:
  TickResult tick() override { return TickResult::None; }
  void render(GfxRenderer&, const Rect&) override {}
  bool renderNative(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override { return StrId::STR_STANDBY_TITLE; }
  uint32_t secondsUntilNextWake() const override { return 3600u; }
  bool wantsClock() const override { return false; }
};

#endif
