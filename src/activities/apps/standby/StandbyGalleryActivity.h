#pragma once

#include <string>
#include <vector>

#include "StandbyPictures.h"
#include "activities/UiListActivity.h"

// Choose a standby or power-off picture from thumbnails laid out in a grid, a page
// at a time. A tile opens the picture full screen, where it can be set as either
// screen and the neighbours flipped through. Thumbnails are made in the background,
// one per loop pass, while placeholders hold their places.
class StandbyGalleryActivity final : public UiListActivity {
 public:
  StandbyGalleryActivity(GfxRenderer& renderer, MappedInputManager& input, StandbyPictures::Target target)
      : UiListActivity("StandbyGallery", renderer, input), target(target) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 protected:
  int listCount() const override { return static_cast<int>(pictures.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

 private:
  enum Tool : int { FilterAll, FilterDefaults, FilterMine, PrevPage, NextPage, Download, Transfer };
  static constexpr freeink::ui::ActionId ACTION_TOOL = ACTION_USER;

  void reload();
  void runTool(int tool);
  void turnPage(int direction);
  // One missing thumbnail of the shown page, then a repaint after the last.
  void makePendingThumbnail();

  StandbyPictures::Target target;
  StandbyPictures::Filter filter = StandbyPictures::Filter::All;
  std::vector<StandbyPictures::Entry> pictures;
  std::string inUse;
  bool listFailed = false;

  // Geometry of the last page drawn (render task), read by the loop under RenderLock.
  int pageCapacity = 6;
  int thumbWidth = 0;
  int thumbHeight = 0;
  int thumbPageStart = -1;
  uint32_t thumbPending = 0;
  uint32_t thumbAttempted = 0;

  char pageText[24] = {};
};
