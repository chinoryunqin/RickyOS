#include "RickyStatsEditor.h"

#include <I18n.h>

#include "components/UITheme.h"

namespace design = RickyDesignUi;
namespace ds = ricky::ds;
namespace fui = freeink::ui;

const char* RickyStatsEditor::confirmLabel() const { return tr(STR_CONFIRM); }

void RickyStatsEditor::onEnter() {
  RickyStatsActivity::onEnter();
  selectedField = 0;
  for (const auto action : {Action::Field, Action::Minus, Action::Plus, Action::Confirm})
    app.on(static_cast<fui::ActionId>(action), &RickyStatsEditor::dispatch, this);
}

void RickyStatsEditor::selectField(int index) {
  selectedField = (index + kFieldCount) % kFieldCount;
  reveal(fieldAreas_[selectedField]);
  requestUpdate();
}

void RickyStatsEditor::dispatch(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<RickyStatsEditor*>(user);
  self.app.clearTapFlash();
  switch (static_cast<Action>(event.action)) {
    case Action::Field:
      if (event.value >= 0 && event.value < kFieldCount) self.selectField(event.value);
      break;
    case Action::Minus:
      self.adjustSelectedValue(-1);
      break;
    case Action::Plus:
      self.adjustSelectedValue(1);
      break;
    case Action::Confirm:
      self.activateField();
      break;
  }
}

bool RickyStatsEditor::handleStatsInput() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Confirm)) {
    activateField();
    return true;
  }
  if (mappedInput.wasReleased(Button::Down)) {
    selectField(selectedField + 1);
    return true;
  }
  if (mappedInput.wasReleased(Button::Up)) {
    selectField(selectedField - 1);
    return true;
  }
  if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Right)) {
    adjustSelectedValue(mappedInput.wasReleased(Button::Left) ? -1 : 1);
    return true;
  }
  return false;
}

int RickyStatsEditor::actionHeight() const {
  return mappedInput.hasTouch()
             ? std::max<int>(ds::layout::kTouchMin, renderer.getLineHeight(design::BODY) + ds::space::k4 * 2)
             : 0;
}

void RickyStatsEditor::buildPage(UiScreen&, RickyStatsPage& page) {
  const auto rect = RickyStatsPage::rect;
  constexpr int pad = RickyStatsPage::pad;
  if (const char* context = contextText(); context && *context) {
    const auto area = page.take(page.measure(context, design::BODY, page.viewport.width));
    design::text(renderer, area, context, design::BODY, false, fui::TextAlign::Left, 16);
  }
  const int width = page.viewport.width - pad * 2;
  for (int i = 0; i < kFieldCount; ++i) {
    const auto value = fieldValue(i);
    const int labelHeight = page.measure(fieldLabel(i), design::CAPTION, width);
    const int valueHeight = page.measure(value.c_str(), design::BODY, width);
    const auto area =
        page.take(std::max<int>(ds::layout::kTouchMin, pad * 2 + labelHeight + ds::space::kTextStackGap + valueHeight));
    fieldAreas_[i] = Rect{0, area.y - page.viewport.y + page.offset, area.width, area.height};
    const bool selected = i == selectedField;
    design::softCard(renderer, area, selected);
    design::text(renderer, rect(area.x + pad, area.y + pad, width, labelHeight), fieldLabel(i), design::CAPTION,
                 selected, fui::TextAlign::Left, 16);
    design::text(renderer,
                 rect(area.x + pad, area.y + pad + labelHeight + ds::space::kTextStackGap, width, valueHeight),
                 value.c_str(), design::BODY, selected, fui::TextAlign::Left, 16);
    page.hit(area, static_cast<fui::ActionId>(Action::Field), i);
  }
  buildDetails(page);
}

void RickyStatsEditor::buildActions(UiScreen& screen, const Rect& area) {
  if (area.height <= 0) return;
  const int width = (area.width - ds::space::kTouchSpacing * 2) / 3;
  for (int i = 0; i < 3; ++i) {
    const auto button =
        RickyStatsPage::rect(area.x + i * (width + ds::space::kTouchSpacing), area.y, width, area.height);
    design::softCard(renderer, button, i == 2);
    if (i == 2) {
      design::text(renderer, button, confirmLabel(), design::BODY, true, fui::TextAlign::Center);
    } else {
      const int x = button.x + button.width / 2, y = button.y + button.height / 2;
      renderer.drawLine(x - ds::space::k3, y, x + ds::space::k3, y, ds::stroke::kEmphasis, true);
      if (i == 1) renderer.drawLine(x, y - ds::space::k3, x, y + ds::space::k3, ds::stroke::kEmphasis, true);
    }
    const auto action = i == 0 ? Action::Minus : (i == 1 ? Action::Plus : Action::Confirm);
    screen.frame().hit(button, static_cast<fui::ActionId>(action), 0, fui::InputTouch);
  }
}

void RickyStatsEditor::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel(), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
