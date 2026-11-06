#pragma once
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ReaderTouchZonesActivity final : public Activity {
 public:
  ReaderTouchZonesActivity(GfxRenderer& r, MappedInputManager& input)
      : Activity("ReaderTouchZones", r, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool wantsGraySettle() const override { return true; }
 private:
  struct Layout {
    Rect header{}, hint{}, enabled{}, choices[2], cells[9], grid{}, reset{}, done{}, note{};
  };
  Layout layout() const;
  void activate(int index);
  void save();
  void navigateBack();
  OptionPopup popup_;
  GfxRenderer::Orientation savedOrientation_ = GfxRenderer::Orientation::Portrait;
  bool choosing_ = true;
  int selected_ = 0;
  int bank_ = 0;
  bool waitConfirmRelease_ = false;
  bool waitTouchRelease_ = false;
  bool warning_ = false;
};
