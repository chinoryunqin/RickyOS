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
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
  void drawFooter() override;

 private:
  enum class Space : uint8_t { Measuring, Ready, Unavailable };
  // Folder behind a content card; the first existing candidate wins.
  static const char* folderFor(int index);
  bool folderMissing = false;
  Space space = Space::Measuring;
  uint64_t sdTotalBytes = 0;
  uint64_t sdFreeBytes = 0;
  std::array<int, 4> counts{-1, -1, -1, -1};  // books, fonts, images, downloads; -1 = unknown
};
#endif
