#pragma once
#ifdef RICKYOS_PRODUCT

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Settings -> System -> Gestures: every touch gesture in one place. The gestures that
// act everywhere (back, Home, control center) can be switched off here; the reading
// gestures are the reader's page-turn and menu options, moved out of Reading.
class RickyGestureSettingsActivity final : public UiListActivity {
 public:
  RickyGestureSettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("RickyGestureSettings", renderer, input) {}

  enum Row : int {
    BackSwipe,
    HomeSwipe,
    ControlSwipe,
    StatusTap,
    TouchTurns,
    NextGesture,
    PrevGesture,
    TurnDirection,
    ReaderMenu,
    RowCount
  };
  // The first reading row; a section caption sits above it and above BackSwipe.
  static constexpr int kFirstReadingRow = TouchTurns;

 protected:
  int listCount() const override { return RowCount; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void render(RenderLock&& lock) override;

 private:
  OptionPopup optionPopup;
};

#endif
