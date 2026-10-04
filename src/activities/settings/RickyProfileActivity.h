#pragma once
#ifdef RICKYOS_PRODUCT
#include "activities/UiListActivity.h"

class RickyProfileActivity final : public UiListActivity {
 public:
  RickyProfileActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("RickyProfile", renderer, input) {}

 protected:
  int listCount() const override { return 4; }  // nickname, avatar, reset avatar, home note
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

 private:
  bool waitForConfirmRelease = false;
  bool failed = false;
};
#endif
