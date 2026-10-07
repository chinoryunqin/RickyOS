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
  int listCount() const override { return 5; }
  void onEnter() override;
  void loop() override;
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
  uint32_t seenScans = 0;                     // background scans already applied to this page

 public:
  // Something changed the card (a delete, an import): count again on the next visit.
  static void invalidateScan();

 private:
  void applyLastScan();
};
#endif
