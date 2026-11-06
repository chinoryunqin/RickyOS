#include "RickyStatsActivity.h"

#include <I18n.h>

#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace ds = ricky::ds;
namespace design = RickyDesignUi;

RickyStatsPage::UiRect RickyStatsPage::rect(int x, int y, int width, int height) {
  return {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width), static_cast<int16_t>(height)};
}

int RickyStatsPage::measure(const char* value, int font, int width, int lines) {
  const int resolved = renderer.textFontId(design::fontFor(renderer, font, value), value);
  const GfxRenderer::TextFontScope face(renderer, resolved);
  measureTarget.setFont(fui::GfxRendererTarget::FONT_BODY, resolved);
  fui::TextStyle style;
  style.font = fui::GfxRendererTarget::FONT_BODY;
  style.maxLines = lines;
  return std::max<int>(measureTarget.lineHeight(style.font),
                       fui::measureWrappedText(measureTarget, value, style, std::max(1, width)).height);
}

RickyStatsPage::UiRect RickyStatsPage::take(int height) {
  const auto result = rect(viewport.x, cursor, viewport.width, height);
  cursor += height + gap;
  return result;
}

void RickyStatsPage::hit(UiRect area, fui::ActionId action, int value) {
  const auto visible = RickyStatsLayout::visibleHit(Rect{area.x, area.y, area.width, area.height}, viewport);
  if (visible.width > 0 && visible.height >= ds::space::kTouchSpacing)
    screen.frame().hit(rect(visible.x, visible.y, visible.width, visible.height), action, value, fui::InputTouch);
}

void RickyStatsActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  resetScroll();
  guardEntryRelease();
  app.setScreen(&RickyStatsActivity::buildScreen, this);
  requestUpdate();
}

bool RickyStatsActivity::buttonsHeld() const {
  using Button = MappedInputManager::Button;
  for (const auto button : {Button::Back, Button::Confirm, Button::Left, Button::Right, Button::Up, Button::Down,
                            Button::PageBack, Button::PageForward})
    if (mappedInput.isPressed(button)) return true;
  int x = 0, y = 0;
  return mappedInput.isScreenTouchHeld(x, y);
}

void RickyStatsActivity::resetScroll() {
  closeRouting();
  scrollOffset_ = 0;
}

void RickyStatsActivity::reveal(const Rect& area) {
  const int current = scrollOffset_.load();
  const int bottom = area.y + area.height;
  int next = current;
  if (area.y < current)
    next = area.y;
  else if (bottom > current + viewportHeight_.load())
    next = bottom - viewportHeight_.load();
  next = std::clamp(next, 0, maxScroll_.load());
  if (next == current) return;
  closeRouting();
  scrollOffset_ = next;
}

bool RickyStatsActivity::scrollInput() {
  using Button = MappedInputManager::Button;
  const auto swipe = mappedInput.wasSwipe();
  const bool down =
      swipe == MappedInputManager::SwipeDir::Up || (scrollWithButtons() && mappedInput.wasReleased(Button::Down));
  const bool up =
      swipe == MappedInputManager::SwipeDir::Down || (scrollWithButtons() && mappedInput.wasReleased(Button::Up));
  if (maxScroll_.load() <= 0 || (!down && !up)) return false;
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down)
    routeTouch(mappedInput);  // Clear the off-target release before rebuilding hit geometry.
  closeRouting();
  app.clearTapFlash();
  const int step = std::max(1, viewportHeight_.load() / 2);
  scrollOffset_ = std::clamp(scrollOffset_.load() + (down ? step : -step), 0, maxScroll_.load());
  requestUpdate();
  return true;
}

void RickyStatsActivity::loop() {
  if (waitForRelease_) {
    waitForRelease_ = buttonsHeld();
    return;  // Consume the inherited gesture's release, too.
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (scrollInput() || handleStatsInput()) return;
  const auto routed = routeTouch(mappedInput);
  if (routed.routed && app.invalidated()) requestUpdate();
}

void RickyStatsActivity::buildScreen(UiScreen& screen, void* user) {
  auto& self = *static_cast<RickyStatsActivity*>(user);
  const auto bounds = self.pageContentRect();
  const auto safe = UITheme::getInstance().getScreenSafeArea(self.renderer, true, false);
  const int left = std::max<int>(ds::space::kPageMargin, safe.x);
  const int right = std::min(self.renderer.getScreenWidth() - ds::space::kPageMargin, safe.x + safe.width);
  Rect viewport{left, bounds.y + ds::space::kBlockGap, std::max(1, right - left - ds::space::k3),
                std::max(1, std::min(bounds.y + bounds.height, safe.y + safe.height) - bounds.y - ds::space::kBlockGap -
                                ds::space::k4)};
  const int actions = self.actionHeight();
  const Rect actionArea{viewport.x, viewport.y + viewport.height - actions, viewport.width, actions};
  if (actions > 0) viewport.height = std::max(1, viewport.height - actions - RickyStatsPage::gap);
  self.viewportHeight_ = viewport.height;
  const int offset = self.scrollOffset_.load();
  RickyStatsPage page(self.renderer, screen, viewport, offset);
  {
    const GfxRenderer::ClipScope clip(self.renderer, viewport.x, viewport.y, viewport.width, viewport.height);
    self.buildPage(screen, page);
  }
  const int contentHeight = page.contentHeight();
  const int maximum = std::max(0, contentHeight - viewport.height);
  self.maxScroll_ = maximum;
  if (offset > maximum) {
    self.scrollOffset_ = maximum;
    self.requestUpdate();
  }
  if (maximum > 0) {
    const int thumbHeight = std::max<int>(ds::space::k8, viewport.height * viewport.height / contentHeight);
    const int thumbY = viewport.y + (viewport.height - thumbHeight) * std::min(offset, maximum) / maximum;
    const int x = viewport.x + viewport.width + ds::space::k2;
    design::fill(self.renderer, RickyStatsPage::rect(x, viewport.y, ds::stroke::kHairline, viewport.height),
                 ds::gray::kLine);
    design::fill(self.renderer, RickyStatsPage::rect(x, thumbY, ds::stroke::kEmphasis, thumbHeight), ds::gray::kInk);
  }
  self.buildActions(screen, actionArea);
}

void RickyStatsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void RickyStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, headerTitle());
  renderUi();
  drawFooter();
  renderer.displayBuffer();
}
