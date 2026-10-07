#include "FrontlightPanelActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/customListIcons.h"
#include "components/icons/listIcons.h"
#ifdef RICKYOS_PRODUCT
#include "ReadingStatsStore.h"
#include "activities/ActivityManager.h"
#include "activities/apps/standby/StandbyActivity.h"
#include "components/RickyPageUi.h"
#include "components/icons/rickyPageIcons.h"
#include "util/TimeUtils.h"
#endif

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_BRIGHTNESS = 1;
constexpr fui::ActionId ACTION_WARMTH = 2;
constexpr fui::ActionId ACTION_TOGGLE = 3;
constexpr fui::ActionId ACTION_BRIGHTNESS_STEP = 4;
constexpr fui::ActionId ACTION_WARMTH_STEP = 5;
constexpr fui::ActionId ACTION_TILE = 6;  // value = tile index

// iOS-style geometry. The panel is a card hanging from the top of the screen:
// a grabber, full-width slider pills, then a 2-column tile grid. The chrome
// itself is fui::sheet / fui::sliderRow / fui::tileGrid; these constants only
// size the bands, and computePanelBottom() mirrors them.
constexpr int16_t kPanelSideMargin = 16;
constexpr int16_t kGrabberHeight = 5;     // fui::SheetProps default, mirrored here
constexpr int16_t kSliderRowHeight = 56;  // the pill itself (finger-sized)
constexpr int16_t kTileHeight = UiHighDpiProfile::enabled ? UiHighDpiProfile::buttonHeight : 84;
constexpr int16_t kTileGap = UiHighDpiProfile::enabled ? UiHighDpiProfile::controlGap : 16;
constexpr int kTileCols = 2;
// One percent per press, on the -/+ buttons and on the physical Left/Right keys
// alike (both repeat while held), so a level can be set exactly.
constexpr int BRIGHTNESS_STEP = 1;
// The dimmest setting is 1%, not 0: turning the light off is what the lamp
// button next to the slider is for, so a 0% "on" level would only be a second,
// worse way to reach the same place.
constexpr uint8_t MIN_BRIGHTNESS = 1;

uint8_t percentFromPermille(const int16_t permille) {
  int value = (static_cast<int>(permille) * 100 + 500) / 1000;
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  return static_cast<uint8_t>(value);
}
}  // namespace

FrontlightPanelActivity::FrontlightPanelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const bool overReader)
    : Activity("FrontlightPanel", renderer, mappedInput), UiAppHost(renderer, true) {
#ifdef RICKYOS_PRODUCT
  this->overReader = overReader;
#else
  (void)overReader;
#endif
}

void FrontlightPanelActivity::onEnter() {
  Activity::onEnter();

  // A stored 0% predates the 1% floor (or came from the web settings): show it
  // as the floor rather than a level the slider can no longer produce. onExit
  // persists that, which is the intent — 0 is not a brightness any more.
  brightness = std::max(MIN_BRIGHTNESS, Frontlight.brightness());
  warmth = Frontlight.warmth();
  lightOn = Frontlight.isOn();
  lightOnChanged = false;
#if FREEINK_DEVICE_EEGO_A4
  // The first frame draws over the reader's gray AA page; a HALF refresh keeps
  // the overlay clean instead of ghosting. Other boards keep the standard
  // FAST_REFRESH first frame (X4 Pro reference behavior).
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
#endif

  // Seed the touch tile's restore mode from the live setting, so toggling off
  // and back on within this session returns to the mode the user had.
  if (SETTINGS.touchReaderControls != CrossPointSettings::TOUCH_READER_OFF) {
    touchModeRestore = SETTINGS.touchReaderControls;
  }

  resetUi();
  // The host app holds six handlers (UiAppHost); RickyOS boards have no frontlight
  // and route every control through the tile action.
#ifdef RICKYOS_PRODUCT
  if (!Frontlight.present()) {
    app.on(ACTION_TILE, &FrontlightPanelActivity::onTileEvent, this);
    app.setScreen(&FrontlightPanelActivity::panelScreen, this);
    requestUpdate();
    return;
  }
#endif
  app.on(ACTION_BRIGHTNESS, &FrontlightPanelActivity::onBrightnessEvent, this);
  app.on(ACTION_WARMTH, &FrontlightPanelActivity::onWarmthEvent, this);
  app.on(ACTION_TOGGLE, &FrontlightPanelActivity::onToggleEvent, this);
  app.on(ACTION_BRIGHTNESS_STEP, &FrontlightPanelActivity::onBrightnessStepEvent, this);
  app.on(ACTION_WARMTH_STEP, &FrontlightPanelActivity::onWarmthStepEvent, this);
  app.on(ACTION_TILE, &FrontlightPanelActivity::onTileEvent, this);
  app.setScreen(&FrontlightPanelActivity::panelScreen, this);
  requestUpdate();
}

void FrontlightPanelActivity::persistLightSettings() {
  // brightness/warmth are always restored unconditionally on boot (see
  // main.cpp), so they never diverge from SETTINGS at onEnter() — comparing
  // against SETTINGS here only fires on a genuine user change. lightOn has
  // no such guarantee (see lightOnChanged's declaration), so it's gated on
  // the user actually having touched it this session instead.
  const bool changed = SETTINGS.frontlightBrightness != brightness || SETTINGS.frontlightWarmth != warmth ||
                       (lightOnChanged && SETTINGS.frontlightOn != (lightOn ? 1 : 0));
  if (changed) {
    SETTINGS.frontlightBrightness = brightness;
    SETTINGS.frontlightWarmth = warmth;
    if (lightOnChanged) SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
  }
}

void FrontlightPanelActivity::onExit() {
  persistLightSettings();
  Activity::onExit();
#if FREEINK_DEVICE_EEGO_A4
  // The overlay pops back to the reader's gray AA page; force a clean first
  // frame so the panel's white region does not ghost over the text.
  renderer.requestNextFullRefresh();
#endif
}

void FrontlightPanelActivity::onBrightnessEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FrontlightPanelActivity*>(user);
  if (event.dragPermille < 0) return;
  self->brightness = std::max(MIN_BRIGHTNESS, percentFromPermille(event.dragPermille));
  Frontlight.setBrightness(self->brightness);
  if (!self->lightOn) {
    self->lightOn = true;
    self->lightOnChanged = true;
    Frontlight.setOn(true);
  }
}

void FrontlightPanelActivity::onWarmthEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FrontlightPanelActivity*>(user);
  if (event.dragPermille < 0) return;
  self->warmth = percentFromPermille(event.dragPermille);
  Frontlight.setWarmth(self->warmth);
}

void FrontlightPanelActivity::onToggleEvent(const fui::ActionEvent&, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->toggleLight();
}

void FrontlightPanelActivity::onBrightnessStepEvent(const fui::ActionEvent& event, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->adjustBrightness(event.value);
}

void FrontlightPanelActivity::onWarmthStepEvent(const fui::ActionEvent& event, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->adjustWarmth(event.value);
}

void FrontlightPanelActivity::onTileEvent(const fui::ActionEvent& event, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->runTile(event.value);
}

void FrontlightPanelActivity::runTile(const int idx) {
#ifdef RICKYOS_PRODUCT
  runRickyTile(static_cast<Tile>(idx));
  return;
#endif
  switch (idx) {
    case 0:  // Night mode (inverted output polarity, applied to the whole UI)
      SETTINGS.screenInverted = SETTINGS.screenInverted ? 0 : 1;
      SETTINGS.saveToFile();
      // Inversion rewrites every pixel; take the clean waveform so the panel
      // does not keep a ghost of the old polarity.
      cleanRefreshPending = true;
      requestUpdate();
      break;
    case 1:  // Ghost-cleanup refresh of the whole frame
      // Refreshing with the panel still up would clean a frame the user is
      // about to dismiss anyway: drop the panel first and let the repaint of
      // the screen underneath carry the clean waveform instead.
      renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
      close();
      break;
    case 2:  // Cycle the reading orientation
      SETTINGS.orientation = static_cast<uint8_t>((SETTINGS.orientation + 1) % 4);
      SETTINGS.saveToFile();
      // Only the setting changes: turning the renderer cropped the portrait-only
      // screens the panel opens over. The reader reflows on its next loop().
      requestUpdate();
      break;
    case 3:  // Touch reader controls (for reading with the palm on the glass)
      // Toggles the existing Settings -> Controls option, nothing lower-level:
      // that setting only governs the reader's tap/swipe handling, so the
      // panel's own gestures (including the swipe that reopens it) keep
      // working while it is off. Off remembers the mode (Tap/Swipe/Inverted
      // Tap) so toggling back does not stomp the user's choice.
      if (SETTINGS.touchReaderControls != CrossPointSettings::TOUCH_READER_OFF) {
        touchModeRestore = SETTINGS.touchReaderControls;
        SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_OFF;
      } else {
        SETTINGS.touchReaderControls = touchModeRestore;
      }
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    default:
      break;
  }
}

void FrontlightPanelActivity::adjustBrightness(const int delta) {
  int next = static_cast<int>(brightness) + delta;
  if (next < MIN_BRIGHTNESS) next = MIN_BRIGHTNESS;
  if (next > 100) next = 100;
  if (next == brightness) return;
  brightness = static_cast<uint8_t>(next);
  Frontlight.setBrightness(brightness);
  if (!lightOn) {
    lightOn = true;
    lightOnChanged = true;
    Frontlight.setOn(true);
  }
  requestUpdate();
}

void FrontlightPanelActivity::adjustWarmth(const int delta) {
  int next = static_cast<int>(warmth) + delta;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  if (next == warmth) return;
  warmth = static_cast<uint8_t>(next);
  Frontlight.setWarmth(warmth);
  requestUpdate();
}

void FrontlightPanelActivity::toggleLight() {
  lightOn = !lightOn;
  lightOnChanged = true;
  Frontlight.setOn(lightOn);
  requestUpdate();
}

void FrontlightPanelActivity::close() { finish(); }

bool FrontlightPanelActivity::handleHomeGesture() {
  close();
  return true;
}

void FrontlightPanelActivity::loop() {
  const auto touch = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) {
      if (touch.event.dragPermille >= 0) draggingSlider = true;
      return;
    }
    // Swipe up dismisses the sheet, the way it was pulled down from the top
    // edge. draggingSlider keeps a fast slider flick from closing it.
    if (!draggingSlider && mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Up) {
      close();
      return;
    }
    // panelBottom > 0 guards the frame the sheet opens in: the release that
    // opened it (a status-bar tap) is still in the input snapshot when the panel
    // runs its first loop(), and panelBottom is only known once render() has
    // measured the layout — so at 0 that release read as "tapped below the
    // sheet" and closed it again before it was ever drawn.
    if (touch.snap.touchReleased && !draggingSlider && panelBottom > 0 && touch.snap.touchY >= panelBottom) {
      close();
      return;
    }
  }
  if (draggingSlider) {
    if (!touch.snap.touchHeld) draggingSlider = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleLight();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                       [this] { adjustBrightness(-BRIGHTNESS_STEP); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                       [this] { adjustBrightness(BRIGHTNESS_STEP); });
}

int FrontlightPanelActivity::computePanelBottom() const {
#ifdef RICKYOS_PRODUCT
  if (!Frontlight.present()) return rickyPanelHeight();
#endif
  const auto tokens = uiThemeTokens(uiTarget, true);
  const auto& metrics = uiThemeMetrics(true);
  const int16_t lineHeight = uiTarget.lineHeight(tokens.smallText.font);
  // Slim battery band + the air around it (mirrors buildPanelScreen).
  const int y0 = std::max<int>(metrics.batteryHeight, lineHeight);
  int y = tokens.spaceMd + y0 + tokens.spaceMd;
  if (Frontlight.present()) {
    // Screen::sliderRow reserves caption + spaceMd + control band, then a
    // spaceMd gap; addSliderRow() adds one more spaceMd of air after each row.
    y += lineHeight + tokens.spaceMd + kSliderRowHeight + 2 * tokens.spaceMd;  // brightness
    if (Frontlight.hasColorTemperature()) {
      y += lineHeight + tokens.spaceMd + kSliderRowHeight + 2 * tokens.spaceMd;  // warmth
    }
    y += tokens.spaceSm;
  }
  // Tiles are touch targets, so a buttons-only board gets no grid and the
  // sheet is exactly the frontlight controls.
  const int tileCount = mappedInput.hasTouch() ? kTileCount : 0;
  y += fui::tileGridHeight(static_cast<uint16_t>(tileCount), kTileCols, kTileHeight, kTileGap);
  // The sheet's grabber band: content margin + grabber + air to the edge.
  // buildPanelScreen() feeds the same theme spacings into SheetProps.
  y += tokens.spaceLg + kGrabberHeight + tokens.spaceLg + tokens.spaceMd;
  return y;
}

void FrontlightPanelActivity::panelScreen(UiScreen& screen, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->buildPanelScreen(screen);
}

void FrontlightPanelActivity::addSliderRow(UiScreen& screen, const char* label, const uint8_t value,
                                           const fui::ActionId sliderAction, const fui::ActionId stepAction,
                                           const bool showToggle) {
  // Live percentage readout. The row draws before this call returns
  // (immediate mode), so borrowing a stack buffer is safe.
  char pct[8];
  snprintf(pct, sizeof(pct), "%u%%", static_cast<unsigned>(value));

  // rowProps is a member (fui::SliderRowProps embeds a 324-byte StyleSet, well
  // past the 256-byte budget a local gets — AGENTS.md). Every field that
  // varies between the two rows is reassigned here; the rest keep their
  // constructed defaults, which already match the panel's card language.
  rowProps.label = label;
  rowProps.value = pct;
  rowProps.sliderValue = value;
  rowProps.sliderAction = sliderAction;
  rowProps.decrement = stepAction;
  rowProps.increment = stepAction;
  rowProps.decrementValue = -BRIGHTNESS_STEP;
  rowProps.incrementValue = BRIGHTNESS_STEP;
  if (showToggle) {
    // Lamp on/off after the +: the sliders set the level, this kills the light
    // outright. Filled glyph = on, outline = off.
    rowProps.toggleAction = ACTION_TOGGLE;
    rowProps.toggleIcon = fui::bitmapFromIcon(lightOn ? icon_sun_filled_32 : icon_sun_32);
  } else {
    rowProps.toggleAction = fui::NO_ACTION;
    rowProps.toggleIcon = fui::BitmapRef{};
  }
  screen.sliderRow(rowProps, kSliderRowHeight);
  // The wrapper's own trailing gap is one spaceMd; double it so the rows
  // breathe — a control band this tall reads cramped at the list cadence.
  screen.spacer(screen.theme().spaceMd);
}

void FrontlightPanelActivity::buildPanelScreen(UiScreen& screen) {
#ifdef RICKYOS_PRODUCT
  buildRickyPanel(screen);
  return;
#endif
  const auto& theme = screen.theme();

  // Sheet chrome first: the card body, the 2px rule along its bottom edge, and
  // the grabber on the edge the sheet is dragged from. Screen::sheet() also
  // clamps the content area to the sheet, so every band below lays out inside
  // it. (No header: the panel is a floating card, and its own grabber says
  // what it is.)
  fui::SheetProps sheetProps;
  // A roomy band above the bottom rule: the grabber gets a full spaceLg of
  // air on both sides so the last row of content never crowds the sheet edge.
  sheetProps.grabberMargin = theme.spaceLg;
  sheetProps.grabberInset = static_cast<int16_t>(theme.spaceLg + theme.spaceMd);
  screen.sheet(sheetProps, static_cast<int16_t>(panelBottom));
  screen.insetContent(fui::Insets{0, kPanelSideMargin, 0, kPanelSideMargin});

  // Reuse the exact battery renderer and header rectangle used by Home. Call
  // the base implementation directly because RoundedRaff suppresses its
  // untitled Home header.
  {
    const auto& metrics = uiThemeMetrics(true);
    screen.spacer(theme.spaceMd);
    const int16_t bandH = std::max<int16_t>(static_cast<int16_t>(metrics.batteryHeight),
                                            screen.target().lineHeight(theme.smallText.font));
    screen.takeTop(bandH, theme.spaceMd);
    GUI.drawHeaderWithStyle(
        renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.homeTopPadding - metrics.topPadding},
        nullptr, nullptr, true, true);
  }

  if (Frontlight.present()) {
    addSliderRow(screen, tr(STR_BRIGHTNESS), brightness, ACTION_BRIGHTNESS, ACTION_BRIGHTNESS_STEP,
                 /*showToggle=*/true);
    if (Frontlight.hasColorTemperature()) {
      addSliderRow(screen, tr(STR_WARMTH), warmth, ACTION_WARMTH, ACTION_WARMTH_STEP, /*showToggle=*/false);
    }
    screen.spacer(theme.spaceSm);
  }

  // Quick-setting tiles. Two columns of finger-sized cards; a tile whose
  // setting is currently on draws filled (StateChecked -> selected style).
  // Touch boards only — the tiles are touch targets.
  if (mappedInput.hasTouch()) {
    static constexpr StrId kOrientNames[4] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                              StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
    // The orientation tile is labelled with just the current mode ("Portrait"):
    // the mode names say what the tile is about on their own.
    const char* orientLabel = I18N.get(kOrientNames[SETTINGS.orientation % 4]);
    const bool touchOn = SETTINGS.touchReaderControls != CrossPointSettings::TOUCH_READER_OFF;

    const char* labels[kTileCount] = {tr(STR_NIGHT_MODE), tr(STR_FORCE_REFRESH), orientLabel, tr(STR_TOUCH_TOGGLE)};
    const fui::State states[kTileCount] = {SETTINGS.screenInverted ? fui::StateChecked : fui::StateNormal,
                                           fui::StateNormal, fui::StateNormal,
                                           // Filled when touch reader controls are OFF — the non-default,
                                           // attention-worthy state.
                                           touchOn ? fui::StateNormal : fui::StateChecked};

    for (int id = 0; id < kTileCount; ++id) {
      gridItems[id].label = labels[id];
      gridItems[id].value = static_cast<int16_t>(id);
      gridItems[id].state = states[id];
      gridItems[id].icon = id == 3 ? GUI.checkboxIcon(touchOn) : fui::BitmapRef{};
    }
    gridProps.items = gridItems;
    gridProps.count = static_cast<uint16_t>(kTileCount);
    gridProps.action = ACTION_TILE;
    gridProps.tileHeight = kTileHeight;
    gridProps.gap = kTileGap;
    gridProps.iconOnRight = true;
    screen.tileGrid(gridProps);
  }
}

#ifdef RICKYOS_PRODUCT
namespace {
// Round actions: a circle per column, the label under it.
constexpr int16_t kActionDiameter = 96;
constexpr int16_t kActionLabelGap = 10;
constexpr int kActionColumns = 4;
// Clean-refresh choices in REFRESH_FREQUENCY order (pages between clean refreshes; 0 = never).
constexpr int kRefreshPages[] = {1, 5, 10, 15, 30, 0};
}  // namespace

void FrontlightPanelActivity::runRickyTile(const Tile tile) {
  switch (tile) {
    case Tile::Night:
      SETTINGS.screenInverted = SETTINGS.screenInverted ? 0 : 1;
      SETTINGS.saveToFile();
      cleanRefreshPending = true;
      requestUpdate();
      break;
    case Tile::Refresh:
      renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
      close();
      break;
    case Tile::Standby:
      // Standby goes on top of the panel, so waking returns to whatever the panel
      // opened over (a book stays open); the panel then closes itself.
      startActivityForResultWith<StandbyActivity>([this](const ActivityResult&) { close(); });
      break;
    case Tile::Transfer:
      activityManager.goToFileTransfer();
      break;
    case Tile::Orientation:
      SETTINGS.orientation = static_cast<uint8_t>((SETTINGS.orientation + 1) % 4);
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case Tile::Clean:
      cycleCleanRefresh();
      break;
  }
}

void FrontlightPanelActivity::cycleCleanRefresh() {
  const int count = static_cast<int>(std::size(kRefreshPages));
  SETTINGS.refreshFrequency = static_cast<uint8_t>((std::min<int>(SETTINGS.refreshFrequency, count - 1) + 1) % count);
  SETTINGS.saveToFile();
  requestUpdate();
}

// Mirrors buildRickyPanel() band by band so the sheet is sized before it is drawn.
int FrontlightPanelActivity::rickyPanelHeight() const {
  const auto tokens = uiThemeTokens(uiTarget, true);
  const auto& metrics = uiThemeMetrics(true);
  const int small = uiTarget.lineHeight(tokens.smallText.font);
  const int title = uiTarget.lineHeight(tokens.titleText.font);
  const int actionRow = kActionDiameter + kActionLabelGap + small;
  int y = tokens.spaceMd + std::max<int>(metrics.batteryHeight, title) + tokens.spaceXs;  // clock and battery
  y += small;                                                                             // date line
  if (mappedInput.hasTouch()) {
    y += 2 * tokens.spaceLg + actionRow;
    if (overReader) y += 2 * tokens.spaceLg + small + tokens.spaceMd + actionRow;
  }
  y += tokens.spaceLg + kGrabberHeight + tokens.spaceLg + tokens.spaceMd;  // grabber band
  return y;
}

void FrontlightPanelActivity::roundActionRow(UiScreen& screen, const RoundAction* actions, const int count) {
  const auto& theme = screen.theme();
  auto& target = screen.target();
  auto label = theme.smallText;
  label.maxLines = 1;
  label.align = fui::TextAlign::Center;
  const int16_t labelHeight = target.lineHeight(theme.smallText.font);
  const auto row = screen.takeTop(static_cast<int16_t>(kActionDiameter + kActionLabelGap + labelHeight), 0);
  const int16_t column = static_cast<int16_t>(row.width / kActionColumns);
  for (int i = 0; i < count; ++i) {
    const RoundAction& action = actions[i];
    const fui::Rect cell{static_cast<int16_t>(row.x + i * column), row.y, column, row.height};
    const fui::Rect circle{static_cast<int16_t>(cell.x + (column - kActionDiameter) / 2), cell.y, kActionDiameter,
                           kActionDiameter};
    constexpr uint8_t radius = kActionDiameter / 2;
    // On = a filled disc with the icon knocked out in white; off = a hairline ring.
    if (action.on) {
      target.fill(circle, fui::Paint::solid(fui::Color::Black), radius);
    } else {
      target.fill(circle, fui::Paint::solid(fui::Color::White), radius);
      target.stroke(circle, fui::Paint::solid(fui::Color::Black), 2, radius);
    }
    const freeink::Icon& icon = *action.icon;
    target.bitmap(fui::Rect{static_cast<int16_t>(circle.x + (kActionDiameter - icon.w) / 2),
                            static_cast<int16_t>(circle.y + kActionDiameter / 2 - icon.opticalCenterY),
                            static_cast<int16_t>(icon.w), static_cast<int16_t>(icon.h)},
                  fui::bitmapFromIcon(icon), fui::BitmapMode::Center,
                  fui::Paint::solid(action.on ? fui::Color::White : fui::Color::Black));
    target.text(fui::Rect{cell.x, static_cast<int16_t>(circle.bottom() + kActionLabelGap), cell.width, labelHeight},
                action.label, label);
    // The whole column (disc and label) is the touch target.
    screen.frame().hit(cell, ACTION_TILE, static_cast<int16_t>(action.tile), fui::InputTouch);
  }
}

void FrontlightPanelActivity::buildRickyPanel(UiScreen& screen) {
  const auto& theme = screen.theme();
  fui::SheetProps sheetProps;
  sheetProps.grabberMargin = theme.spaceLg;
  sheetProps.grabberInset = static_cast<int16_t>(theme.spaceLg + theme.spaceMd);
  screen.sheet(sheetProps, static_cast<int16_t>(panelBottom));
  screen.insetContent(fui::Insets{0, theme.spaceLg, 0, theme.spaceLg});
  auto& target = screen.target();
  RickyPageUi::Bold bold(renderer, 1);
  auto small = theme.smallText;
  small.maxLines = 1;
  const int16_t smallHeight = target.lineHeight(theme.smallText.font);

  // Clock on the left of the status band, battery on the right where Home draws it.
  const auto& metrics = uiThemeMetrics(true);
  screen.spacer(theme.spaceMd);
  const auto band = screen.takeTop(
      std::max<int16_t>(static_cast<int16_t>(metrics.batteryHeight), target.lineHeight(theme.titleText.font)),
      theme.spaceXs);
  GUI.drawHeaderWithStyle(
      renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.homeTopPadding - metrics.topPadding},
      nullptr, nullptr, true, true);
  std::tm local{};
  const bool haveTime = halClock.localTime(local);
  if (!haveTime || !TimeUtils::formatCurrentTime(timeText, sizeof(timeText), SETTINGS.clockFormat == 1)) {
    snprintf(timeText, sizeof(timeText), "--:--");
  }
  auto clock = theme.titleText;
  clock.bold = true;
  clock.maxLines = 1;
  target.text(fui::Rect{band.x, band.y, static_cast<int16_t>(band.width / 2), band.height}, timeText, clock);
  char date[40] = {};
  if (haveTime) {
    static constexpr StrId weekdays[] = {
        StrId::STR_CAL_WEEKDAY_SUN, StrId::STR_CAL_WEEKDAY_MON, StrId::STR_CAL_WEEKDAY_TUE, StrId::STR_CAL_WEEKDAY_WED,
        StrId::STR_CAL_WEEKDAY_THU, StrId::STR_CAL_WEEKDAY_FRI, StrId::STR_CAL_WEEKDAY_SAT};
    snprintf(date, sizeof(date), tr(STR_RICKY_HOME_DATE), static_cast<unsigned>(local.tm_mon + 1),
             static_cast<unsigned>(local.tm_mday), I18N.get(weekdays[std::clamp(local.tm_wday, 0, 6)]));
  }
  char today[40];
  snprintf(today, sizeof(today), tr(STR_RICKY_HOME_TODAY),
           static_cast<unsigned>(READING_STATS.getTodayReadingMs() / 60000));
  snprintf(infoText, sizeof(infoText), haveTime ? "%s · %s" : "%s%s", date, today);
  target.text(screen.takeTop(smallHeight, 0), infoText, small);

  // Round actions are touch targets; a buttons-only board keeps just the clock.
  if (!mappedInput.hasTouch()) return;
  const auto rule = [&] {
    screen.spacer(theme.spaceLg);
    const auto line = screen.takeTop(1, 0);
    target.fill(line, fui::Paint::solid(fui::Color::Black));
    screen.spacer(theme.spaceLg);
  };

  rule();
  const bool night = SETTINGS.screenInverted != 0;
  const RoundAction common[] = {
      {Tile::Night, &icon_ricky_night_40, tr(STR_NIGHT_MODE), night},
      {Tile::Refresh, &icon_ricky_refresh_40, tr(STR_FORCE_REFRESH), false},
      {Tile::Standby, &icon_ricky_standby_40, tr(STR_RICKY_STANDBY_NOW), false},
      {Tile::Transfer, &icon_ricky_transfer_40, tr(STR_FILE_TRANSFER), false},
  };
  roundActionRow(screen, common, static_cast<int>(std::size(common)));

  if (!overReader) return;
  // Reading: only over a book, where each of these changes the page underneath.
  rule();
  target.text(screen.takeTop(smallHeight, theme.spaceMd), tr(STR_RICKY_CC_READING), small);
  static constexpr StrId kOrientNames[4] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                            StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
  const uint8_t orientation = SETTINGS.orientation % 4;
  const bool portrait = orientation == 0 || orientation == 2;
  const int refreshIndex = std::min<int>(SETTINGS.refreshFrequency, static_cast<int>(std::size(kRefreshPages)) - 1);
  if (kRefreshPages[refreshIndex] == 0) {
    snprintf(refreshText, sizeof(refreshText), "%s", tr(STR_RICKY_CC_FULL_REFRESH_OFF));
  } else {
    snprintf(refreshText, sizeof(refreshText), tr(STR_RICKY_CC_FULL_REFRESH), kRefreshPages[refreshIndex]);
  }
  const RoundAction reading[] = {
      {Tile::Orientation, portrait ? &icon_ricky_portrait_40 : &icon_ricky_landscape_40,
       I18N.get(kOrientNames[orientation]), false},
      {Tile::Clean, &icon_ricky_clean_40, refreshText, false},
  };
  roundActionRow(screen, reading, static_cast<int>(std::size(reading)));
}
#endif

void FrontlightPanelActivity::render(RenderLock&&) {
  panelBottom = computePanelBottom();

  // fui::sheet draws the card body, its bottom rule, and the grabber during
  // renderUi(); the battery band at the card's top is part of the screen build.
  renderUi();

  // A tile that rewrote the whole frame (night mode) re-drives every pixel
  // once; ordinary repaints stay on the fast path. HALF: strong enough to
  // flip the whole frame's polarity without the FULL waveform's blackout
  // flash. Any faint residue clears with the panel's dedicated refresh tile
  // or the next scheduled clean refresh.
#if FREEINK_DEVICE_EEGO_A4
  renderer.displayBuffer(cleanRefreshPending ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
#else
  renderer.displayBuffer(cleanRefreshPending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
#endif
  cleanRefreshPending = false;
}
