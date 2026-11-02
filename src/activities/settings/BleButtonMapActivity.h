#pragma once

#include <I18n.h>

#include "BleKeyMapping.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class BleButtonMapActivity final : public UiListActivity {
 public:
  BleButtonMapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("BleButtonMap", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  bool keepsBluetoothAlive() const override { return true; }

 private:
  struct Function {
    bleinput::Action action;
    StrId label;
  };
  static constexpr uint8_t kFunctionCount = static_cast<uint8_t>(bleinput::Action::Count);
  static const Function kFunctions[kFunctionCount];
  static constexpr uint8_t kRowCount = kFunctionCount + 1;

  int captureTarget = -1;
  freeink::ui::ListItem rowItems[kRowCount]{};
  char valueBuffers[kFunctionCount][32]{};
  const char* unavailable = nullptr;
  OptionPopup resetPopup;

  int listCount() const override { return captureTarget >= 0 ? 0 : kRowCount; }
  void buildScreen(UiScreen& screen) override;
  void render(RenderLock&&) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
  const char* headerTitle() const override { return tr(STR_BT_MAP_BUTTONS); }
  void drawChrome() override;
};
