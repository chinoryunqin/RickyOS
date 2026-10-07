#include "RickyGestureSettingsActivity.h"
#ifdef RICKYOS_PRODUCT
#include <GfxRenderer.h>
#include <I18n.h>

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

constexpr StrId kRowLabels[RickyGestureSettingsActivity::RowCount] = {
    StrId::STR_RICKY_GESTURE_BACK,   StrId::STR_RICKY_GESTURE_HOME,       StrId::STR_RICKY_GESTURE_CONTROL,
    StrId::STR_RICKY_GESTURE_STATUS, StrId::STR_RICKY_GESTURE_TOUCH_TURN, StrId::STR_NEXT_PAGE_GESTURE,
    StrId::STR_PREV_PAGE_GESTURE,    StrId::STR_PAGE_TURN_DIRECTION,      StrId::STR_SHOW_READER_MENU};

// On/off rows flip on a tap; the rest pick from a list.
uint8_t* toggleFor(const int row) {
  switch (row) {
    case RickyGestureSettingsActivity::BackSwipe:
      return &SETTINGS.rickyGestureBack;
    case RickyGestureSettingsActivity::HomeSwipe:
      return &SETTINGS.rickyGestureHome;
    case RickyGestureSettingsActivity::ControlSwipe:
      return &SETTINGS.rickyGestureControl;
    case RickyGestureSettingsActivity::StatusTap:
      return &SETTINGS.rickyGestureStatus;
    case RickyGestureSettingsActivity::TouchTurns:
      return &SETTINGS.touchReaderControls;
    default:
      return nullptr;
  }
}

const char* valueFor(const int row) {
  if (const uint8_t* toggle = toggleFor(row)) return I18N.get(*toggle ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
  switch (row) {
    case RickyGestureSettingsActivity::NextGesture:
      return I18N.get(kGestureLabels[SETTINGS.pageTurnGesture % std::size(kGestureLabels)]);
    case RickyGestureSettingsActivity::PrevGesture:
      return I18N.get(kGestureLabels[SETTINGS.previousPageGesture % std::size(kGestureLabels)]);
    case RickyGestureSettingsActivity::TurnDirection:
      return I18N.get(kDirectionLabels[SETTINGS.pageTurnDirection % std::size(kDirectionLabels)]);
    case RickyGestureSettingsActivity::ReaderMenu:
      return I18N.get(kMenuLabels[SETTINGS.showReaderMenu % std::size(kMenuLabels)]);
    default:
      return "";
  }
}
}  // namespace

const char* RickyGestureSettingsActivity::headerTitle() const { return tr(STR_RICKY_GESTURES); }

bool RickyGestureSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void RickyGestureSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void RickyGestureSettingsActivity::buildScreen(UiScreen& screen) {
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
  for (int i = 0; i < RowCount; ++i) {
    if (i == BackSwipe || i == kFirstReadingRow) {
      if (i == kFirstReadingRow) screen.spacer(static_cast<int16_t>(gap));
      target.text(screen.takeTop(static_cast<int16_t>(smallHeight), static_cast<int16_t>(gap)),
                  I18N.get(i == BackSwipe ? StrId::STR_RICKY_GESTURE_GLOBAL : StrId::STR_RICKY_GESTURE_READING),
                  caption);
    }
    const auto row = screen.takeTop(static_cast<int16_t>(rowHeight), static_cast<int16_t>(gap));
    RickyPageUi::card(target, row, focus && selected == i);
    screen.frame().hit(row, ACTION_ROW, i, fui::InputTouch);
    const int middle = row.y + row.height / 2;
    const int textX = row.x + gap * 2;
    const int valueWidth = row.width / 4;
    const int valueX = row.right() - gap * 2 - valueWidth;
    target.text(fui::Rect{static_cast<int16_t>(textX), static_cast<int16_t>(middle - bodyHeight / 2),
                          static_cast<int16_t>(valueX - textX - gap), static_cast<int16_t>(bodyHeight)},
                I18N.get(kRowLabels[i]), label);
    target.text(fui::Rect{static_cast<int16_t>(valueX), static_cast<int16_t>(middle - smallHeight / 2),
                          static_cast<int16_t>(valueWidth), static_cast<int16_t>(smallHeight)},
                valueFor(i), value);
  }
}

void RickyGestureSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= RowCount || optionPopup.isActive()) return;
  app.clearTapFlash();
  nav.selected = index;
  if (uint8_t* toggle = toggleFor(index)) {
    *toggle = *toggle ? 0 : 1;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }
  const auto pick = [](uint8_t& field) {
    return [&field](const int chosen) {
      field = static_cast<uint8_t>(chosen);
      SETTINGS.saveToFile();
    };
  };
  switch (index) {
    case NextGesture:
      optionPopup.show(StrId::STR_NEXT_PAGE_GESTURE, kGestureLabels, static_cast<int>(std::size(kGestureLabels)),
                       SETTINGS.pageTurnGesture % std::size(kGestureLabels), pick(SETTINGS.pageTurnGesture));
      break;
    case PrevGesture:
      optionPopup.show(StrId::STR_PREV_PAGE_GESTURE, kGestureLabels, static_cast<int>(std::size(kGestureLabels)),
                       SETTINGS.previousPageGesture % std::size(kGestureLabels), pick(SETTINGS.previousPageGesture));
      break;
    case TurnDirection:
      optionPopup.show(StrId::STR_PAGE_TURN_DIRECTION, kDirectionLabels, static_cast<int>(std::size(kDirectionLabels)),
                       SETTINGS.pageTurnDirection % std::size(kDirectionLabels), pick(SETTINGS.pageTurnDirection));
      break;
    case ReaderMenu:
      optionPopup.show(StrId::STR_SHOW_READER_MENU, kMenuLabels, static_cast<int>(std::size(kMenuLabels)),
                       SETTINGS.showReaderMenu % std::size(kMenuLabels), pick(SETTINGS.showReaderMenu));
      break;
    default:
      return;
  }
  requestUpdate();
}
#endif
