#pragma once

#include "Epub/converters/ImageToneCurve.h"
#include "ReaderImagePanelLayout.h"
#include "ReaderImageSettings.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

class Section;
class ImageBlock;
class ReaderImageSettingsActivity final : public Activity, private UiAppHost {
 public:
  enum class Result : int32_t { Preview, Original, Applied };
  ReaderImageSettingsActivity(GfxRenderer& renderer, MappedInputManager& input, const std::string& book,
                              ReaderImageDraft& draft, Section* section);
  ~ReaderImageSettingsActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool wantsGraySettle() const override { return true; }
  bool isReaderActivity() const override { return true; }
  bool handleHomeGesture() override;

 private:
  enum class Focus : uint8_t {
    Contrast,
    Brightness,
    Smooth,
    Scope,
    Original,
    Photo,
    Comic,
    Scan,
    Compare,
    Next,
    Apply,
    Cancel,
    Count
  };
  const std::string& book_;
  ReaderImageDraft& draft_;
  Section* section_;
  // Keep just one ImageBlock from the existing page deserializer, not a Page
  // or a second bitmap. Moving ownership adds no allocation; release on exit.
  std::unique_ptr<ImageBlock> source_;
  int imageIndex_ = 0;
  int imageCount_ = 0;
  bool sourceLoadFailed_ = false;
  bool initialSmooth_ = false;
  OptionPopup popup_;
  ButtonNavigator navigator_;
  bool waitForRelease_ = false;
  bool saveFailed_ = false;
  bool comparing_ = false;
  bool dragging_ = false;
  bool previewPending_ = false;
  unsigned long changedAt_ = 0;
  Focus focus_ = Focus::Contrast;
  bool showFocus_ = false;
  // Fixed inline LUT/control storage avoids heap churn and large task-stack
  // frames (ButtonProps embeds a StyleSet). No additional framebuffer.
  ImageToneCurve curve_;
  freeink::ui::ButtonProps button_;
  freeink::ui::SliderProps slider_;
  readerImagePanel::Layout layout_;
  static void screenFn(UiScreen& screen, void* user);
  static void actionFn(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);
  void drawButton(UiScreen& screen, readerImagePanel::Box box, const char* text, freeink::ui::ActionId action,
                  int value, Focus focus, bool checked = false, bool enabled = true);
  void drawTone(UiScreen& screen, readerImagePanel::Box box, bool contrast);
  void drawPreview(UiScreen& screen);
  void loadImage();
  void activate(Focus focus);
  void setTone(bool contrast, int value);
  void markEdited();
  void cancel();
  void finishWith(Result result);
};
