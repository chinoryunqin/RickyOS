#pragma once
#ifdef RICKYOS_PRODUCT

#include <string>

#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"
#include "util/RickyStorageLayout.h"

// The Standby & power-off page. Standby tab: what the screen shows while the device
// waits (the picture, the book's cover, the calendar clock, or the page as it was),
// the picture and how it sits, the corner, when Standby starts, and "Standby now".
// Power-off tab: what stays on the screen once the device is off, and when Standby
// powers off by itself.
class RickyStandbySettingsActivity final : public UiTabListActivity {
 public:
  RickyStandbySettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiTabListActivity("RickyStandbySettings", renderer, input) {}

  enum Tab : int { StandbyTab, PowerOffTab, TabCount };
  enum Row : uint8_t {
    Style,
    ChoosePicture,
    DownloadPictures,
    PictureFit,
    PictureFilter,
    StandbyInfo,
    AutoStandby,
    StandbyNow,
    PowerOffScreen,
    AutoPowerOff,
    RowKinds
  };

 protected:
  void onEnter() override;
  int listCount() const override { return rowCount_; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void render(RenderLock&& lock) override;

  int tabCount() const override { return TabCount; }
  int activeTab() const override { return tab_; }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;

 private:
  // The rows the active tab shows now; the Standby tab's depend on its style.
  void rebuildRows();
  const char* rowValue(Row row) const;
  void drawPreview(const Rect& box) const;
  void openPicturePicker(const std::string& folder = RickyStorageLayout::IMAGES);

  OptionPopup optionPopup;
  int tab_ = StandbyTab;
  Row rows_[RowKinds] = {};
  int rowCount_ = 0;
  char minutes_[24] = {};  // the auto-standby value when it is not one of the presets
};

#endif
