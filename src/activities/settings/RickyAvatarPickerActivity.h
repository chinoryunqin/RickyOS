#pragma once
#ifdef RICKYOS_PRODUCT
#include "activities/UiListActivity.h"
#include "components/icons/rickyAvatars.h"

// Brand badge, the built-in pixel portraits, then "import from the SD card".
class RickyAvatarPickerActivity final : public UiListActivity {
 public:
  RickyAvatarPickerActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("RickyAvatarPicker", renderer, input) {}

 protected:
  int listCount() const override { return 1 + RickyAvatars::COUNT + 1; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

 private:
  static constexpr int IMPORT = 1 + RickyAvatars::COUNT;
  bool failed = false;
  bool waitForConfirmRelease = false;
};
#endif
