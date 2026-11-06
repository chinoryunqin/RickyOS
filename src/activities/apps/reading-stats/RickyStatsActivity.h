#pragma once

#include <atomic>

#include "activities/Activity.h"
#include "components/RickyDesignUi.h"
#include "components/RickyStatsLayout.h"

// A measured vertical document, independent of the viewport height. This stack
// adapter owns no text or pixel buffers; drawing and hit testing share its clip.
class RickyStatsPage {
 public:
  using UiScreen = UiAppHost::UiScreen;
  using UiRect = freeink::ui::Rect;
  static constexpr int gap = ricky::ds::space::kGutter;
  static constexpr int pad = ricky::ds::space::kCardPadding;

  RickyStatsPage(const GfxRenderer& renderer, UiScreen& screen, Rect viewport, int offset)
      : renderer(renderer),
        screen(screen),
        viewport(viewport),
        offset(offset),
        cursor(viewport.y - offset),
        measureTarget(renderer) {}
  static UiRect rect(int x, int y, int width, int height);
  int measure(const char* value, int font, int width, int lines = 16);
  int lineHeight(int font) const { return renderer.getLineHeight(font); }
  UiRect take(int height);
  void hit(UiRect area, freeink::ui::ActionId action, int value = 0);
  int contentHeight() const { return std::max(0, cursor - viewport.y + offset - gap); }
  const GfxRenderer& renderer;
  UiScreen& screen;
  const Rect viewport;
  const int offset;

 private:
  int cursor;
  RickyUiTarget measureTarget;
};

// The statistics family is a document, not a fixed-height list. One owner of
// safe margins, typography measurement, clipping, scrolling and touch routing.
class RickyStatsActivity : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool wantsGraySettle() const override { return true; }

 protected:
  static constexpr freeink::ui::ActionId ACTION_USER = 2;
  RickyStatsActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input)
      : Activity(name, renderer, input), UiAppHost(renderer) {}
  virtual const char* headerTitle() const = 0;
  virtual void buildPage(UiScreen& screen, RickyStatsPage& page) = 0;
  virtual bool handleStatsInput() { return false; }
  virtual bool scrollWithButtons() const { return true; }
  virtual int actionHeight() const { return 0; }
  virtual void buildActions(UiScreen&, const Rect&) {}
  virtual void drawFooter();
  void resetScroll();
  void reveal(const Rect& area);
  void guardEntryRelease() { waitForRelease_ = true; }

 private:
  static void buildScreen(UiScreen& screen, void* user);
  bool scrollInput();
  bool buttonsHeld() const;
  std::atomic<int> scrollOffset_{0};
  std::atomic<int> maxScroll_{0};
  std::atomic<int> viewportHeight_{0};
  bool waitForRelease_ = true;
};
