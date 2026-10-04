#pragma once
#ifdef RICKYOS_PRODUCT
#include <array>

#include "activities/UiListActivity.h"

class RickyProfileActivity final : public UiListActivity {
 public:
  RickyProfileActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("RickyProfile", renderer, input) {}

 protected:
  int listCount() const override { return 3; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

 private:
  std::array<freeink::ui::ListItem, 3> rows{};
  bool waitForConfirmRelease = false;
  bool failed = false;
};
#endif
