#include "ReaderImageSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "components/RickyDesignTokens.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId kActivate = 1, kContrast = 2, kBrightness = 3, kStepContrast = 4, kStepBrightness = 5,
                        kSmooth = 6;
constexpr StrId kScopeLabels[] = {StrId::STR_READER_IMAGE_BOOK, StrId::STR_READER_IMAGE_DEFAULT,
                                  StrId::STR_READER_IMAGE_FOLLOW};
constexpr StrId kPresetLabels[] = {StrId::STR_READER_IMAGE_NEUTRAL, StrId::STR_READER_IMAGE_PHOTO,
                                   StrId::STR_READER_IMAGE_COMIC, StrId::STR_READER_IMAGE_SCAN};
fui::Rect rect(readerImagePanel::Box box) {
  return {static_cast<int16_t>(box.x), static_cast<int16_t>(box.y), static_cast<int16_t>(box.width),
          static_cast<int16_t>(box.height)};
}
bool buttonsHeld(const MappedInputManager& input) {
  for (uint8_t button = 0; button < MappedInputManager::kButtonCount; ++button)
    if (input.isPressed(static_cast<MappedInputManager::Button>(button))) return true;
  return false;
}
}  // namespace

ReaderImageSettingsActivity::ReaderImageSettingsActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                         const std::string& book, ReaderImageDraft& draft,
                                                         Section* section)
    : Activity("ReaderImageSettings", renderer, input),
      UiAppHost(renderer),
      book_(book),
      draft_(draft),
      section_(section) {}
ReaderImageSettingsActivity::~ReaderImageSettingsActivity() = default;

void ReaderImageSettingsActivity::onEnter() {
  Activity::onEnter();
  initialSmooth_ = draft_.profile.smooth;
  waitForRelease_ = true;
  resetUi();
  for (auto action : {kActivate, kContrast, kBrightness, kStepContrast, kStepBrightness, kSmooth})
    app.on(action, actionFn, this);
  app.setScreen(screenFn, this);
  loadImage();
  requestUpdate();
}
void ReaderImageSettingsActivity::onExit() {
  closeRouting();
  source_.reset();
  Activity::onExit();
}
void ReaderImageSettingsActivity::loadImage() {
  source_.reset();
  imageCount_ = 0;
  sourceLoadFailed_ = false;
  if (!section_) return;
  // Use the reader's bounded deserializer once per selection. All text blocks
  // and the temporary Page are released immediately; only the selected image
  // and its two path strings survive until selection changes or exit.
  auto page = section_->loadPage(section_->currentPage);
  if (!page) {
    sourceLoadFailed_ = true;
    LOG_ERR("IMG", "Cannot load preview page");
    return;
  }
  for (auto& element : page->elements) {
    if (element->getTag() != TAG_PageImage) continue;
    if (imageCount_++ == imageIndex_) source_ = static_cast<PageImage&>(*element).takeImageBlock();
  }
}
void ReaderImageSettingsActivity::cancel() {
  app.clearTapFlash();
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
bool ReaderImageSettingsActivity::handleHomeGesture() {
  cancel();
  return true;
}
void ReaderImageSettingsActivity::finishWith(Result result) {
  app.clearTapFlash();
  setResult(IntervalResult{static_cast<int32_t>(result)});
  finish();
}
void ReaderImageSettingsActivity::markEdited() {
  if (draft_.scope == ReaderImageScope::FollowDefault) draft_.scope = ReaderImageScope::Book;
  saveFailed_ = false;
  comparing_ = false;
  previewPending_ = true;
  changedAt_ = millis();
}
void ReaderImageSettingsActivity::setTone(bool contrast, int value) {
  const int limit = contrast ? 50 : 40;
  auto& current = contrast ? draft_.profile.contrast : draft_.profile.brightness;
  value = std::clamp(value, -limit, limit);
  if (current == value) return;
  current = static_cast<int8_t>(value);
  markEdited();
}
void ReaderImageSettingsActivity::activate(Focus focus) {
  focus_ = focus;
  switch (focus) {
    case Focus::Contrast:
      setTone(true, draft_.profile.contrast + 5);
      return;
    case Focus::Brightness:
      setTone(false, draft_.profile.brightness + 5);
      return;
    case Focus::Smooth:
      draft_.profile.smooth = !draft_.profile.smooth;
      markEdited();
      return;
    case Focus::Scope:
      popup_.show(StrId::STR_READER_IMAGE_SCOPE, kScopeLabels, 3, static_cast<int>(draft_.scope), [this](int value) {
        draft_.scope = static_cast<ReaderImageScope>(value);
        if (draft_.scope == ReaderImageScope::FollowDefault) draft_.profile = readerImageSettings::defaults();
        saveFailed_ = false;
        comparing_ = false;
        waitForRelease_ = buttonsHeld(mappedInput);
        requestUpdate();
      });
      requestUpdate();
      return;
    case Focus::Original:
      draft_.profile = {0, readerImageBrightness::kOriginal, initialSmooth_};
      markEdited();
      return;
    case Focus::Photo:
      draft_.profile = {10, 0, true};
      markEdited();
      return;
    case Focus::Comic:
      draft_.profile = {20, 0, true};
      markEdited();
      return;
    case Focus::Scan:
      draft_.profile = {15, 5, true};
      markEdited();
      return;
    case Focus::Compare:
      comparing_ = !comparing_;
      requestUpdate();
      return;
    case Focus::Next:
      if (imageCount_ > 1) {
        imageIndex_ = (imageIndex_ + 1) % imageCount_;
        loadImage();
        requestUpdate();
      }
      return;
    case Focus::Apply:
      if (readerImageSettings::save(book_, draft_))
        finishWith(Result::Applied);
      else {
        saveFailed_ = true;
        requestUpdate();
      }
      return;
    case Focus::Cancel:
      cancel();
      return;
    case Focus::Count:
      return;
  }
}
void ReaderImageSettingsActivity::actionFn(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<ReaderImageSettingsActivity*>(user);
  self.showFocus_ = false;
  self.app.clearTapFlash();
  switch (event.action) {
    case kActivate:
      if (event.value >= 0 && event.value < static_cast<int>(Focus::Count))
        self.activate(static_cast<Focus>(event.value));
      return;
    case kContrast:
    case kBrightness:
      if (event.dragPermille >= 0) {
        self.dragging_ = true;
        const bool contrast = event.action == kContrast;
        self.focus_ = contrast ? Focus::Contrast : Focus::Brightness;
        self.setTone(contrast, readerImagePanel::toneFromPermille(event.dragPermille, contrast ? 50 : 40));
      }
      return;
    case kSmooth:
      self.focus_ = Focus::Smooth;
      self.draft_.profile.smooth = event.value == 1;
      self.markEdited();
      return;
    case kStepContrast:
      self.focus_ = Focus::Contrast;
      self.setTone(true, self.draft_.profile.contrast + event.value * 5);
      return;
    case kStepBrightness:
      self.focus_ = Focus::Brightness;
      self.setTone(false, self.draft_.profile.brightness + event.value * 5);
      return;
    default:
      return;  // SDK action identifiers are an open set.
  }
}
void ReaderImageSettingsActivity::loop() {
  if (waitForRelease_) {
    waitForRelease_ = buttonsHeld(mappedInput);
    return;
  }
  // The render task owns decode. Serialize parameter and source changes with
  // it so a published image always matches the controls painted alongside it.
  RenderLock lock(*this);
  if (popup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  const auto route = routeTouch(mappedInput, false, true);
  if (dragging_) {
    if (route.snap.touchHeld || route.snap.touchPressed) return;
    dragging_ = false;  // consume drag-release/swipe fallout before Back routing
    if (previewPending_) changedAt_ = millis();
    return;
  }
  if (route) return;
  if (previewPending_ && millis() - changedAt_ >= 300) {
    previewPending_ = false;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showFocus_ = true;
    activate(focus_);
    return;
  }
  navigator_.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    showFocus_ = true;
    focus_ = static_cast<Focus>((static_cast<int>(focus_) + static_cast<int>(Focus::Count) - 1) %
                                static_cast<int>(Focus::Count));
    requestUpdate();
  });
  navigator_.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
    showFocus_ = true;
    focus_ = static_cast<Focus>((static_cast<int>(focus_) + 1) % static_cast<int>(Focus::Count));
    requestUpdate();
  });
  const auto adjust = [this](int delta) {
    showFocus_ = true;
    if (focus_ == Focus::Contrast || focus_ == Focus::Brightness) {
      const bool contrast = focus_ == Focus::Contrast;
      setTone(contrast, (contrast ? draft_.profile.contrast : draft_.profile.brightness) + delta);
    } else if (focus_ == Focus::Smooth) {
      draft_.profile.smooth = delta > 0;
      markEdited();
    }
  };
  navigator_.onPressAndContinuous({MappedInputManager::Button::Left}, [&] { adjust(-5); });
  navigator_.onPressAndContinuous({MappedInputManager::Button::Right}, [&] { adjust(5); });
}
void ReaderImageSettingsActivity::screenFn(UiScreen& screen, void* user) {
  static_cast<ReaderImageSettingsActivity*>(user)->buildScreen(screen);
}
void ReaderImageSettingsActivity::drawButton(UiScreen& screen, readerImagePanel::Box box, const char* text,
                                             fui::ActionId action, int value, Focus focus, bool checked, bool enabled) {
  button_.label = text;
  button_.action = action;
  button_.value = value;
  button_.inputMask = fui::InputTouch;
  button_.enabled = enabled;
  button_.minTouchSize = static_cast<int16_t>(box.height);
  button_.text = screen.theme().smallText;
  button_.radius = screen.theme().controlRadius;
  button_.styles = screen.theme().button;
  button_.state = checked ? fui::StateSelected : fui::StateNormal;
  if (showFocus_ && focus_ == focus) button_.state = static_cast<fui::State>(button_.state | fui::StateFocused);
  fui::button(screen.frame(), rect(box), button_);
  // The native panel has no distinct tint fill. Keep neutral actions visibly
  // bounded using the same rectangle that FUI registered for touch.
  if (ricky::panel::nativeSmall && enabled && !checked && !(showFocus_ && focus_ == focus)) {
    screen.target().stroke(rect(box), fui::Paint::solid(fui::Color::DarkGray), ricky::ds::stroke::kHairline,
                           screen.theme().controlRadius);
  }
}
void ReaderImageSettingsActivity::drawTone(UiScreen& screen, readerImagePanel::Box box, bool contrast) {
  char value[12];
  const int current = contrast ? draft_.profile.contrast : draft_.profile.brightness;
  snprintf(value, sizeof(value), "%+d", current);
  auto text = screen.theme().smallText;
  const int caption = screen.target().lineHeight(text.font);
  fui::Rect label{static_cast<int16_t>(box.x), static_cast<int16_t>(box.y), static_cast<int16_t>(box.width),
                  static_cast<int16_t>(caption)};
  text.bold = showFocus_ && focus_ == (contrast ? Focus::Contrast : Focus::Brightness);
  screen.target().text(label, contrast ? tr(STR_READER_IMAGE_CONTRAST) : tr(STR_READER_IMAGE_BRIGHTNESS), text);
  text.align = fui::TextAlign::Right;
  screen.target().text(label, value, text);
  readerImagePanel::Box band{box.x, box.y + caption + layout_.gap, box.width, layout_.control};
  const Focus focus = contrast ? Focus::Contrast : Focus::Brightness;
  const auto step = contrast ? kStepContrast : kStepBrightness;
  drawButton(screen, {band.x, band.y, band.height, band.height}, "-", step, -1, focus);
  drawButton(screen, {band.right() - band.height, band.y, band.height, band.height}, "+", step, 1, focus);
  slider_.action = contrast ? kContrast : kBrightness;
  slider_.inputMask = fui::InputTouch | fui::InputDrag;
  slider_.value = current + (contrast ? 50 : 40);
  slider_.max = contrast ? 100 : 80;
  slider_.minTouchSize = static_cast<int16_t>(band.height);
  slider_.knobHeight = static_cast<int16_t>(std::max(22, band.height / 2));
  const readerImagePanel::Box track{band.x + band.height + layout_.gap, band.y,
                                    band.width - 2 * (band.height + layout_.gap), band.height};
  fui::slider(screen.frame(), rect(track), slider_);
}
void ReaderImageSettingsActivity::drawPreview(UiScreen& screen) {
  const auto box = layout_.preview;
  screen.target().stroke(rect(box), fui::Paint::solid(fui::Color::DarkGray), 1);
  const char* message = sourceLoadFailed_ ? tr(STR_READER_IMAGE_LOAD_FAILED) : tr(STR_READER_IMAGE_NO_PICTURE);
  if (source_ && box.width > 16 && box.height > 16) {
    const int width = box.width - 16, height = box.height - 16;
    const int sourceW = source_->getWidth(), sourceH = source_->getHeight();
    if (sourceW > 0 && sourceH > 0) {
      const int w = std::max(1, std::min(width, height * sourceW / sourceH));
      const int h = std::max(1, std::min(height, width * sourceH / sourceW));
      RenderConfig config;
      config.x = box.x + (box.width - w) / 2;
      config.y = box.y + (box.height - h) / 2;
      config.maxWidth = w;
      config.maxHeight = h;
      config.useExactDimensions = true;
      config.bilinearScaling = draft_.profile.smooth;
      config.preserveAlpha = true;
      config.useDithering = !renderer.isGrayscale16Active();
      config.output =
          renderer.isGrayscale16Active() ? DecodeOutput::NativeGrayscale16 : DecodeOutput::FrameBufferAndCache;
      curve_.set(comparing_ ? 0 : draft_.profile.contrast, comparing_ ? 0 : draft_.profile.renderBrightness());
      config.toneLut = curve_.data();
      if (source_->renderPreview(renderer, config)) return;
      LOG_ERR("IMG", "Inline preview decode failed");
    }
    message = tr(STR_READER_IMAGE_LOAD_FAILED);
  }
  auto text = screen.theme().smallText;
  text.align = fui::TextAlign::Center;
  screen.target().text(rect(box), message, text);
}
void ReaderImageSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer);
  const auto body =
      SubpageLayout::insetHorizontal(SubpageLayout::contentRect(safe, metrics, false, metrics.buttonHintsHeight),
                                     std::max(8, metrics.contentSidePadding));
  const int caption = screen.target().lineHeight(screen.theme().smallText.font);
  const int gap = std::max<int>(8, screen.theme().spaceSm);
  layout_ = readerImagePanel::layout({body.x, body.y, body.width, body.height}, caption,
                                     std::max(44, screen.theme().minTouchSize * 3 / 4), gap);
  drawPreview(screen);
  drawButton(screen, layout_.compare, comparing_ ? tr(STR_READER_IMAGE_EFFECT) : tr(STR_READER_IMAGE_COMPARE),
             kActivate, static_cast<int>(Focus::Compare), Focus::Compare, comparing_, source_ != nullptr);
  char next[48];
  snprintf(next, sizeof(next), "%s %d/%d", tr(STR_READER_IMAGE_NEXT), imageCount_ ? imageIndex_ + 1 : 0, imageCount_);
  drawButton(screen, layout_.next, next, kActivate, static_cast<int>(Focus::Next), Focus::Next, false, imageCount_ > 1);
  for (int i = 0; i < 4; ++i) {
    const auto focus = static_cast<Focus>(static_cast<int>(Focus::Original) + i);
    const ReaderImageProfile preset[] = {
        {0, readerImageBrightness::kOriginal, initialSmooth_}, {10, 0, true}, {20, 0, true}, {15, 5, true}};
    drawButton(screen, layout_.presets[i], I18N.get(kPresetLabels[i]), kActivate, static_cast<int>(focus), focus,
               draft_.profile == preset[i]);
  }
  drawTone(screen, layout_.contrast, true);
  drawTone(screen, layout_.brightness, false);
  drawButton(screen, layout_.smooth[0], tr(STR_READER_IMAGE_SMOOTH_OFF), kSmooth, 0, Focus::Smooth,
             !draft_.profile.smooth);
  drawButton(screen, layout_.smooth[1], tr(STR_READER_IMAGE_SMOOTH_STANDARD_LABEL), kSmooth, 1, Focus::Smooth,
             draft_.profile.smooth);
  // Scope keeps its own full-width band; no adjacent invisible touch overlap.
  char scope[128];
  snprintf(scope, sizeof(scope), "%s: %s", tr(STR_READER_IMAGE_SCOPE),
           I18N.get(kScopeLabels[static_cast<int>(draft_.scope)]));
  drawButton(screen, layout_.scope, scope, kActivate, static_cast<int>(Focus::Scope), Focus::Scope);
  drawButton(screen, layout_.cancel, tr(STR_CANCEL), kActivate, static_cast<int>(Focus::Cancel), Focus::Cancel);
  drawButton(screen, layout_.apply, saveFailed_ ? tr(STR_READER_IMAGE_SAVE_FAILED) : tr(STR_READER_IMAGE_APPLY),
             kActivate, static_cast<int>(Focus::Apply), Focus::Apply, true);
}
void ReaderImageSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer);
  GUI.drawHeader(renderer, SubpageLayout::headerRect(safe, metrics), tr(STR_READER_IMAGE_SETTINGS));
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}
