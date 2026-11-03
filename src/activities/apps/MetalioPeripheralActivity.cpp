#include "MetalioPeripheralActivity.h"
#if FREEINK_DEVICE_METALIO_EINK4
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "activities/util/ImageViewerActivity.h"
#include "components/UITheme.h"
#include "standby/StandbyGalleryActivity.h"

namespace fui = freeink::ui;
namespace {
const char* stateName(HalCellular::State state) {
  using S = HalCellular::State;
  switch (state) {
    case S::Off:
      return tr(STR_CELL_OFF);
    case S::Starting:
      return tr(STR_CELL_STARTING);
    case S::Connected:
      return tr(STR_CELL_CONNECTED);
    case S::NoSim:
      return tr(STR_CELL_NO_SIM);
    case S::Denied:
      return tr(STR_CELL_DENIED);
    case S::Searching:
      return tr(STR_CELL_SEARCHING);
    case S::Error:
      return tr(STR_CELL_ERROR);
  }
  return "?";
}
}  // namespace
MetalioPeripheralActivity::MetalioPeripheralActivity(GfxRenderer& renderer, MappedInputManager& input, bool camera)
    : UiListActivity("Metalio peripherals", renderer, input), camera_(camera) {}
const char* MetalioPeripheralActivity::headerTitle() const {
  return camera_ ? tr(STR_CAMERA_TITLE) : tr(STR_CELL_TITLE);
}
void MetalioPeripheralActivity::onEnter() {
  cell_ = HalCellular::snapshot();
  photo_ = HalCamera::snapshot();
  UiListActivity::onEnter();
}
void MetalioPeripheralActivity::onExit() {
  if (camera_) HalCamera::leave();
  UiListActivity::onExit();
}
void MetalioPeripheralActivity::prepareForSleep() {
  if (camera_) HalCamera::prepareForSleep();
}
bool MetalioPeripheralActivity::preventAutoSleep() { return camera_ ? HalCamera::busy() : HalCellular::busy(); }
bool MetalioPeripheralActivity::handleHomeGesture() { return camera_ && HalCamera::busy(); }
void MetalioPeripheralActivity::onBackButton() {
  if (!camera_ || !HalCamera::busy()) finish();
}
void MetalioPeripheralActivity::loop() {
  if (millis() - lastPoll_ >= 500) {
    lastPoll_ = millis();
    const auto cell = HalCellular::snapshot();
    const auto photo = HalCamera::snapshot();
    if (memcmp(&cell_, &cell, sizeof(cell)) || memcmp(&photo_, &photo, sizeof(photo))) {
      RenderLock lock;
      cell_ = cell;
      photo_ = photo;
      requestUpdate();
    }
  }
  UiListActivity::loop();
}
void MetalioPeripheralActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (auto& value : values_) value[0] = 0;
  if (camera_) {
    const StrId ids[] = {StrId::STR_CELL_STATUS, StrId::STR_CAMERA_SHUTTER, StrId::STR_CAMERA_PREVIEW,
                         StrId::STR_GALLERY_TITLE, StrId::STR_CAMERA_USB_NOTE};
    for (int i = 0; i < 5; ++i) rows_[i].label = I18N.get(ids[i]);
    const char* status = tr(STR_CAMERA_READY);
    switch (photo_.state) {
      case HalCamera::State::Working:
        status = tr(STR_CAMERA_CAPTURING);
        break;
      case HalCamera::State::Saved:
        status = tr(STR_CAMERA_SAVED);
        break;
      case HalCamera::State::Error:
        status = tr(STR_CELL_ERROR);
        break;
      default:
        break;
    }
    snprintf(values_[0], sizeof(values_[0]), "%s", status);
    if (photo_.state == HalCamera::State::Saved)
      snprintf(values_[2], sizeof(values_[2]), "%ux%u · %lu KB", photo_.width, photo_.height,
               static_cast<unsigned long>(photo_.bytes / 1024));
    else if (photo_.state == HalCamera::State::Error)
      snprintf(values_[0], sizeof(values_[0]), "%s (%d)", status, photo_.error);
  } else {
    const StrId ids[] = {StrId::STR_CELL_STATUS, StrId::STR_CELL_CARRIER, StrId::STR_CELL_SIGNAL,  StrId::STR_CELL_SIM,
                         StrId::STR_CELL_IP,     StrId::STR_CELL_ENABLE,  StrId::STR_CELL_DISABLE, StrId::STR_CELL_SIM1,
                         StrId::STR_CELL_SIM2,   StrId::STR_CELL_REFRESH};
    for (int i = 0; i < 10; ++i) rows_[i].label = I18N.get(ids[i]);
    snprintf(values_[0], sizeof(values_[0]), "%s", cell_.busy ? tr(STR_CELL_STARTING) : stateName(cell_.state));
    snprintf(values_[1], sizeof(values_[1]), "%s", cell_.carrier);
    if (cell_.signal >= 0 && cell_.signal <= 31)
      snprintf(values_[2], sizeof(values_[2]), "%d dBm", -113 + cell_.signal * 2);
    if (cell_.simSlot < 2) snprintf(values_[3], sizeof(values_[3]), "SIM %u", cell_.simSlot + 1);
    snprintf(values_[4], sizeof(values_[4]), "%s", cell_.ip);
  }
  for (int i = 0; i < listCount(); ++i) {
    rows_[i].value = values_[i];
    rows_[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps props;
  props.items = rows_;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
void MetalioPeripheralActivity::activateIndex(int index) {
  if (camera_) {
    if (HalCamera::busy()) return;
    if (index == 1)
      HalCamera::capture();
    else if (index == 2 && photo_.path[0]) {
      app.clearTapFlash();
      startActivityForResultWith<ImageViewerActivity>([](const ActivityResult&) {}, std::string(photo_.path));
    } else if (index == 3) {
      app.clearTapFlash();
      startActivityForResultWith<StandbyGalleryActivity>([](const ActivityResult&) {}, StandbyPictures::Target::Standby,
                                                         StandbyGalleryActivity::Mode::Browse);
    }
  } else {
    if (cell_.busy && index != 6) return;
    switch (index) {
      case 5:
        HalCellular::enable(true);
        break;
      case 6:
        HalCellular::enable(false);
        break;
      case 7:
        HalCellular::selectSim(0);
        break;
      case 8:
        HalCellular::selectSim(1);
        break;
      case 9:
        HalCellular::refresh();
        break;
      default:
        break;
    }
  }
  requestUpdate();
}
#endif
