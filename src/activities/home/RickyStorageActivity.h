#pragma once
#ifdef RICKYOS_PRODUCT
#include <array>

#include "activities/UiListActivity.h"

class RickyStorageActivity final : public UiListActivity {
 public:
  RickyStorageActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("RickyStorage", renderer, input) {}
  MainTab mainTab() const override { return MainTab::StorageFiles; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override {
    nav.requestSelection(MainTabs::contentEdgeIndex(edge, listCount()));
  }

 protected:
  int listCount() const override { return 6; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
  void drawFooter() override;

 private:
  bool folderMissing = false;
};
#endif
