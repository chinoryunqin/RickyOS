#include "RickyOptionListActivity.h"
#ifdef RICKYOS_PRODUCT
#include <GfxRenderer.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "components/RickyPageUi.h"
#include "components/UITheme.h"

namespace {
namespace fui = freeink::ui;

constexpr StrId kGestureLabels[] = {StrId::STR_TAP_AND_SWIPE, StrId::STR_TAP_ONLY, StrId::STR_SWIPE_ONLY,
                                    StrId::STR_INVERTED_TAP, StrId::STR_DISABLED};
static_assert(std::size(kGestureLabels) == CrossPointSettings::PAGE_TURN_GESTURE_COUNT);
constexpr StrId kDirectionLabels[] = {StrId::STR_PAGE_TURN_LTR, StrId::STR_PAGE_TURN_RTL};
constexpr StrId kMenuLabels[] = {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE_UP};
static_assert(std::size(kMenuLabels) == CrossPointSettings::SHOW_READER_MENU_COUNT);
constexpr StrId kSideKeyLabels[] = {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED,
                                    StrId::STR_NEXT_NEXT, StrId::STR_PREV_PREV};
constexpr StrId kLongPressLabels[] = {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                                      StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION};

template <size_t N>
constexpr uint8_t countOf(const StrId (&)[N]) {
  return static_cast<uint8_t>(N);
}
}  // namespace

namespace RickyOptionLists {
const RickyOptionList& gestures() {
  static const RickyOptionRow rows[] = {
      {StrId::STR_RICKY_GESTURE_BACK, &SETTINGS.rickyGestureBack, nullptr, 0, StrId::STR_RICKY_GESTURE_GLOBAL},
      {StrId::STR_RICKY_GESTURE_HOME, &SETTINGS.rickyGestureHome},
      {StrId::STR_RICKY_GESTURE_CONTROL, &SETTINGS.rickyGestureControl},
      {StrId::STR_RICKY_GESTURE_STATUS, &SETTINGS.rickyGestureStatus},
      {StrId::STR_RICKY_GESTURE_TOUCH_TURN, &SETTINGS.touchReaderControls, nullptr, 0,
       StrId::STR_RICKY_GESTURE_READING},
      {StrId::STR_NEXT_PAGE_GESTURE, &SETTINGS.pageTurnGesture, kGestureLabels, countOf(kGestureLabels)},
      {StrId::STR_PREV_PAGE_GESTURE, &SETTINGS.previousPageGesture, kGestureLabels, countOf(kGestureLabels)},
      {StrId::STR_PAGE_TURN_DIRECTION, &SETTINGS.pageTurnDirection, kDirectionLabels, countOf(kDirectionLabels)},
      {StrId::STR_SHOW_READER_MENU, &SETTINGS.showReaderMenu, kMenuLabels, countOf(kMenuLabels)},
  };
  static const RickyOptionList list{StrId::STR_RICKY_GESTURES, rows, static_cast<int>(std::size(rows))};
  return list;
}

const RickyOptionList& keys() {
  // The strip under the screen is left / middle / right; the middle key is Back.
  static const RickyOptionRow rows[] = {
      {StrId::STR_RICKY_KEYS_SIDE, &SETTINGS.sideButtonLayout, kSideKeyLabels, countOf(kSideKeyLabels)},
      {StrId::STR_RICKY_KEYS_LONG_PRESS, &SETTINGS.longPressButtonBehavior, kLongPressLabels,
       countOf(kLongPressLabels)},
      {StrId::STR_RICKY_KEYS_STANDBY, &SETTINGS.standbyShortcutEnabled},
  };
  static const RickyOptionList list{StrId::STR_RICKY_KEYS, rows, static_cast<int>(std::size(rows))};
  return list;
}
}  // namespace RickyOptionLists

const char* RickyOptionListActivity::headerTitle() const { return I18N.get(list.title); }

bool RickyOptionListActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void RickyOptionListActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void RickyOptionListActivity::buildScreen(UiScreen& screen) {
  const auto bounds = pageContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(bounds.y), 0,
                  static_cast<int16_t>(renderer.getScreenHeight() - bounds.y - bounds.height), 0});
  const auto& theme = screen.theme();
  auto& target = screen.target();
  screen.insetContent(fui::Insets{static_cast<int16_t>(theme.spaceMd), theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int gap = std::max<int>(10, theme.spaceSm);
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  const int smallHeight = target.lineHeight(theme.smallText.font);
  const int selected = RickyPageUi::syncNav(nav, listCount());
  const bool focus = showMainTabContentSelection();
  RickyPageUi::Bold bold(renderer, 1);

  auto label = theme.bodyText;
  label.maxLines = 1;
  auto value = theme.smallText;
  value.maxLines = 1;
  value.align = fui::TextAlign::Right;
  auto caption = theme.smallText;
  caption.maxLines = 1;

  const int rowHeight = bodyHeight + gap * 2;
  for (int i = 0; i < list.count; ++i) {
    const RickyOptionRow& item = list.rows[i];
    if (item.section != StrId::STR_NONE_OPT) {
      if (i > 0) screen.spacer(static_cast<int16_t>(gap));
      target.text(screen.takeTop(static_cast<int16_t>(smallHeight), static_cast<int16_t>(gap)), I18N.get(item.section),
                  caption);
    }
    const auto row = screen.takeTop(static_cast<int16_t>(rowHeight), static_cast<int16_t>(gap));
    RickyPageUi::card(target, row, focus && selected == i);
    screen.frame().hit(row, ACTION_ROW, i, fui::InputTouch);
    const int middle = row.y + row.height / 2;
    const int textX = row.x + gap * 2;
    const int valueWidth = row.width * 2 / 5;
    const int valueX = row.right() - gap * 2 - valueWidth;
    target.text(fui::Rect{static_cast<int16_t>(textX), static_cast<int16_t>(middle - bodyHeight / 2),
                          static_cast<int16_t>(valueX - textX - gap), static_cast<int16_t>(bodyHeight)},
                I18N.get(item.label), label);
    const char* current = item.options ? I18N.get(item.options[*item.field % item.optionCount])
                                       : I18N.get(*item.field ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    target.text(fui::Rect{static_cast<int16_t>(valueX), static_cast<int16_t>(middle - smallHeight / 2),
                          static_cast<int16_t>(valueWidth), static_cast<int16_t>(smallHeight)},
                current, value);
  }
}

void RickyOptionListActivity::activateIndex(const int index) {
  if (index < 0 || index >= list.count || optionPopup.isActive()) return;
  app.clearTapFlash();
  nav.selected = index;
  const RickyOptionRow& item = list.rows[index];
  if (!item.options) {
    *item.field = *item.field ? 0 : 1;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }
  uint8_t* field = item.field;
  optionPopup.show(item.label, item.options, item.optionCount, *field % item.optionCount, [field](const int chosen) {
    *field = static_cast<uint8_t>(chosen);
    SETTINGS.saveToFile();
  });
  requestUpdate();
}
#endif
