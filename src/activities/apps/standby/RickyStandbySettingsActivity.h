#pragma once
#ifdef RICKYOS_PRODUCT

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// The Apps → Standby page: choose what the screen shows while the device sleeps
// (the lock-screen mode, the picture, the default pictures to download) and what the
// full-screen Standby adds on top; "Standby now" opens that full-screen view.
class RickyStandbySettingsActivity final : public UiListActivity {
 public:
  RickyStandbySettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("RickyStandbySettings", renderer, input) {}

  enum Row : int { ScreenMode, ChoosePicture, DownloadPictures, StandbyInfo, StandbyNow, RowCount };

 protected:
  int listCount() const override { return RowCount; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void render(RenderLock&& lock) override;

 private:
  void drawPreview(const Rect& box) const;
  void openPicturePicker();

  OptionPopup optionPopup;
};

#endif
