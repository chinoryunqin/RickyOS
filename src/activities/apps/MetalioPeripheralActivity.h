#pragma once
#include <BoardConfig.h>
#if FREEINK_DEVICE_METALIO_EINK4
#include <HalCamera.h>
#include <HalCellular.h>

#include "activities/UiListActivity.h"

class MetalioPeripheralActivity final : public UiListActivity {
 public:
  MetalioPeripheralActivity(GfxRenderer& renderer, MappedInputManager& input, bool camera);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void prepareForSleep() override;
  bool preventAutoSleep() override;
  bool handleHomeGesture() override;

 private:
  int listCount() const override { return camera_ ? 5 : 10; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool camera_;
  uint32_t lastPoll_ = 0;
  HalCellular::Snapshot cell_;
  HalCamera::Snapshot photo_;
  freeink::ui::ListItem rows_[10]{};
  char values_[10][160]{};
};
#endif
