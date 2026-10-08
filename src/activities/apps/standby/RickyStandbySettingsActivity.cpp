#include "RickyStandbySettingsActivity.h"
#ifdef RICKYOS_PRODUCT
#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>

#include "CrossPointSettings.h"
#include "RickyWallpaperDownloadActivity.h"
#include "StandbyActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ImageViewerActivity.h"
#include "components/RickyPageUi.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RickyStorageLayout.h"

namespace {
namespace fui = freeink::ui;
using Settings = CrossPointSettings;
constexpr char kPicture[] = "/sleep.bmp";

// Standby styles in the order people pick them; stored values differ (RICKY_STANDBY_FACE).
constexpr uint8_t kStyles[] = {Settings::RICKY_STANDBY_PICTURE, Settings::RICKY_STANDBY_COVER,
                               Settings::RICKY_STANDBY_CALENDAR, Settings::RICKY_STANDBY_KEEP_PAGE};
constexpr StrId kStyleLabels[] = {StrId::STR_RICKY_STANDBY_STYLE_PICTURE, StrId::STR_RICKY_STANDBY_STYLE_COVER,
                                  StrId::STR_RICKY_STANDBY_STYLE_CALENDAR, StrId::STR_RICKY_STANDBY_STYLE_KEEP};
static_assert(std::size(kStyles) == Settings::RICKY_STANDBY_FACE_COUNT);
static_assert(std::size(kStyleLabels) == std::size(kStyles));
constexpr StrId kFitLabels[] = {StrId::STR_RICKY_PICTURE_FIT_WHOLE, StrId::STR_RICKY_PICTURE_FIT_FILL};
static_assert(std::size(kFitLabels) == Settings::SLEEP_SCREEN_COVER_MODE_COUNT);
constexpr StrId kFilterLabels[] = {StrId::STR_NONE_OPT, StrId::STR_RICKY_PICTURE_FILTER_CONTRAST,
                                   StrId::STR_RICKY_PICTURE_FILTER_INVERT};
static_assert(std::size(kFilterLabels) == Settings::SLEEP_SCREEN_COVER_FILTER_COUNT);
constexpr StrId kInfoLabels[] = {StrId::STR_RICKY_STANDBY_INFO_NONE, StrId::STR_RICKY_STANDBY_INFO_DATE,
                                 StrId::STR_RICKY_STANDBY_INFO_TIME};
static_assert(std::size(kInfoLabels) == Settings::STANDBY_OVERLAY_COUNT);
// Minutes before Standby; SLEEP_TIMEOUT_NEVER_MINUTES means never.
constexpr uint8_t kStandbyMinutes[] = {1, 3, 5, 10, 15, 30, Settings::SLEEP_TIMEOUT_NEVER_MINUTES};
constexpr StrId kStandbyMinuteLabels[] = {StrId::STR_RICKY_MINUTES_1,  StrId::STR_RICKY_MINUTES_3,
                                          StrId::STR_RICKY_MINUTES_5,  StrId::STR_RICKY_MINUTES_10,
                                          StrId::STR_RICKY_MINUTES_15, StrId::STR_RICKY_MINUTES_30,
                                          StrId::STR_RICKY_NEVER};
static_assert(std::size(kStandbyMinuteLabels) == std::size(kStandbyMinutes));
constexpr StrId kAutoOffLabels[] = {StrId::STR_RICKY_NEVER,   StrId::STR_RICKY_HOURS_1,  StrId::STR_RICKY_HOURS_3,
                                    StrId::STR_RICKY_HOURS_6, StrId::STR_RICKY_HOURS_12, StrId::STR_RICKY_HOURS_24};
static_assert(std::size(kAutoOffLabels) == Settings::RICKY_AUTO_OFF_COUNT);
constexpr StrId kPowerOffLabels[] = {StrId::STR_RICKY_POWER_OFF_SAME, StrId::STR_RICKY_SLEEP_LIGHT,
                                     StrId::STR_RICKY_SLEEP_BLANK};
static_assert(std::size(kPowerOffLabels) == Settings::RICKY_POWER_OFF_SCREEN_COUNT);

constexpr StrId kRowLabels[] = {StrId::STR_RICKY_STANDBY_STYLE,  StrId::STR_RICKY_SELECT_WALLPAPER,
                                StrId::STR_RICKY_WALLPAPER_DOWNLOAD, StrId::STR_RICKY_PICTURE_FIT,
                                StrId::STR_RICKY_PICTURE_FILTER, StrId::STR_RICKY_STANDBY_INFO,
                                StrId::STR_RICKY_AUTO_STANDBY,   StrId::STR_RICKY_STANDBY_NOW,
                                StrId::STR_RICKY_POWER_OFF_SCREEN, StrId::STR_RICKY_AUTO_OFF};
static_assert(std::size(kRowLabels) == RickyStandbySettingsActivity::RowKinds);

int styleIndex() {
  for (size_t i = 0; i < std::size(kStyles); ++i)
    if (kStyles[i] == SETTINGS.rickyStandbyFace) return static_cast<int>(i);
  return 0;
}

int standbyMinutesIndex() {
  for (size_t i = 0; i < std::size(kStandbyMinutes); ++i)
    if (kStandbyMinutes[i] == SETTINGS.sleepTimeoutMinutes) return static_cast<int>(i);
  return -1;
}

// The picture and the cover sit on the screen with the same fit, filter and corner.
bool showsPicture() {
  return SETTINGS.rickyStandbyFace == Settings::RICKY_STANDBY_PICTURE ||
         SETTINGS.rickyStandbyFace == Settings::RICKY_STANDBY_COVER;
}

template <size_t N>
void showOption(OptionPopup& popup, const StrId title, const StrId (&labels)[N], const int current,
                std::function<void(int)> apply) {
  popup.show(title, labels, static_cast<int>(N), current, [apply = std::move(apply)](const int chosen) {
    apply(chosen);
    SETTINGS.saveToFile();
  });
}
}  // namespace

void RickyStandbySettingsActivity::onEnter() {
  rebuildRows();
  UiTabListActivity::onEnter();
}

const char* RickyStandbySettingsActivity::headerTitle() const { return tr(STR_RICKY_POWER_PAGE_TITLE); }

const char* RickyStandbySettingsActivity::tabLabel(const int index) const {
  return index == PowerOffTab ? tr(STR_RICKY_TAB_POWER_OFF) : tr(STR_RICKY_TAB_STANDBY);
}

void RickyStandbySettingsActivity::rebuildRows() {
  rowCount_ = 0;
  auto add = [this](const Row row) { rows_[rowCount_++] = row; };
  if (tab_ == PowerOffTab) {
    add(PowerOffScreen);
    add(AutoPowerOff);
    return;
  }
  add(Style);
  if (SETTINGS.rickyStandbyFace == Settings::RICKY_STANDBY_PICTURE) {
    add(ChoosePicture);
    add(DownloadPictures);
  }
  if (showsPicture()) {
    add(PictureFit);
    add(PictureFilter);
    add(StandbyInfo);
  }
  add(AutoStandby);
  add(StandbyNow);
}

void RickyStandbySettingsActivity::onTabAction(const int index) {
  if (optionPopup.isActive() || index < 0 || index >= TabCount) return;
  if (tab_ != index) {
    tab_ = index;
    rebuildRows();
    auto& n = activeNav();
    n.selected = 0;  // tab taps land with the tab bar focused
    n.followOnBuild = true;
    requestUpdate();
  }
  app.clearTapFlash();
}

void RickyStandbySettingsActivity::stepTab(const int direction) {
  onTabAction((tab_ + (direction < 0 ? TabCount - 1 : 1)) % TabCount);
}

bool RickyStandbySettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] {
    rebuildRows();  // a new style shows different rows
    requestUpdate();
  });
}

bool RickyStandbySettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }
  return false;
}

void RickyStandbySettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiTabListActivity::render(std::move(lock));
}

const char* RickyStandbySettingsActivity::rowValue(const Row row) const {
  switch (row) {
    case Style:
      return I18N.get(kStyleLabels[styleIndex()]);
    case PictureFit:
      return I18N.get(kFitLabels[std::min<int>(SETTINGS.sleepScreenCoverMode, std::size(kFitLabels) - 1)]);
    case PictureFilter:
      return I18N.get(kFilterLabels[std::min<int>(SETTINGS.sleepScreenCoverFilter, std::size(kFilterLabels) - 1)]);
    case StandbyInfo:
      return I18N.get(kInfoLabels[std::min<int>(SETTINGS.standbyOverlay, std::size(kInfoLabels) - 1)]);
    case AutoStandby: {
      const int preset = standbyMinutesIndex();
      if (preset >= 0) return I18N.get(kStandbyMinuteLabels[preset]);
      // A value set before the presets (any minute from 1 to 30).
      auto* self = const_cast<RickyStandbySettingsActivity*>(this);
      snprintf(self->minutes_, sizeof(minutes_), tr(STR_RICKY_MINUTES_N),
               static_cast<unsigned>(SETTINGS.sleepTimeoutMinutes));
      return minutes_;
    }
    case PowerOffScreen:
      return I18N.get(
          kPowerOffLabels[std::min<int>(SETTINGS.rickyPowerOffScreen, std::size(kPowerOffLabels) - 1)]);
    case AutoPowerOff:
      return I18N.get(kAutoOffLabels[std::min<int>(SETTINGS.rickyAutoOffIndex, std::size(kAutoOffLabels) - 1)]);
    default:
      return nullptr;
  }
}

// A small 9:16 picture of what the screen shows in Standby, or once powered off.
void RickyStandbySettingsActivity::drawPreview(const Rect& box) const {
  renderer.drawRoundedRect(box.x, box.y, box.width, box.height, 2, 10, true);
  const bool off = tab_ == PowerOffTab;
  const bool sameAsStandby = !off || SETTINGS.rickyPowerOffScreen == Settings::RICKY_POWER_OFF_SAME;
  const bool picture = sameAsStandby && SETTINGS.rickyStandbyFace == Settings::RICKY_STANDBY_PICTURE;
  if (picture) {
    HalFile file;
    if (Storage.openFileForRead("STANDBY", kPicture, file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const int innerWidth = box.width - 8;
        const int innerHeight = box.height - 8;
        const float scale = std::min(static_cast<float>(innerWidth) / bitmap.getWidth(),
                                     static_cast<float>(innerHeight) / bitmap.getHeight());
        const int width = static_cast<int>(std::lround(bitmap.getWidth() * scale));
        const int height = static_cast<int>(std::lround(bitmap.getHeight() * scale));
        renderer.drawBitmap(bitmap, box.x + (box.width - width) / 2, box.y + (box.height - height) / 2, width, height,
                            0, 0);
        return;
      }
    }
  }
  // Nothing to draw small: name what the screen shows inside the frame.
  const char* label = picture         ? tr(STR_RICKY_STANDBY_EMPTY)
                      : sameAsStandby ? I18N.get(kStyleLabels[styleIndex()])
                                      : I18N.get(kPowerOffLabels[std::min<int>(SETTINGS.rickyPowerOffScreen, 2)]);
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  UITheme::drawCenteredText(renderer, box, SMALL_FONT_ID, box.y + (box.height - lineHeight) / 2, label);
}

void RickyStandbySettingsActivity::buildScreen(UiScreen& screen) {
  const auto bounds = pageContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(bounds.y), 0,
                  static_cast<int16_t>(renderer.getScreenHeight() - bounds.y - bounds.height), 0});
  buildTabBar(screen);
  const auto& theme = screen.theme();
  auto& target = screen.target();
  screen.insetContent(fui::Insets{static_cast<int16_t>(theme.spaceLg), theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int gap = std::max<int>(12, theme.spaceSm);
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  // Ring position 0 is the tab bar; rows follow.
  const int selected = RickyPageUi::syncNav(activeNav(), rowCount_ + 1) - 1;
  const bool focus = showMainTabContentSelection();
  RickyPageUi::Bold bold(renderer, 1);

  // Rows shrink a little when the Standby tab lists all of them, so the preview keeps room.
  const int rowGap = rowCount_ > 6 ? gap * 3 / 4 : gap;
  const int rowHeight = bodyHeight + rowGap * 2;
  const int rowsHeight = rowHeight * rowCount_ + rowGap * (rowCount_ - 1);
  const int previewHeight = std::clamp(static_cast<int>(screen.body().height) - rowsHeight - gap * 3, 160, 560);
  const auto previewBlock = screen.takeTop(previewHeight, gap * 2);
  const int previewWidth = previewHeight * 9 / 16;
  drawPreview(
      Rect{previewBlock.x + (previewBlock.width - previewWidth) / 2, previewBlock.y, previewWidth, previewHeight});

  auto label = theme.bodyText;
  label.maxLines = 1;
  auto value = theme.smallText;
  value.maxLines = 1;
  value.align = fui::TextAlign::Right;
  const int smallHeight = target.lineHeight(theme.smallText.font);
  for (int i = 0; i < rowCount_; ++i) {
    const auto row = screen.takeTop(rowHeight, i + 1 < rowCount_ ? rowGap : 0);
    RickyPageUi::card(target, row, focus && selected == i);
    screen.frame().hit(row, ACTION_ROW, i, fui::InputTouch);
    const int middle = row.y + row.height / 2;
    const int textX = row.x + gap * 2;
    const int chevronX = row.right() - 24 - gap;
    target.text(fui::Rect{static_cast<int16_t>(textX), static_cast<int16_t>(middle - bodyHeight / 2),
                          static_cast<int16_t>(row.width / 2), static_cast<int16_t>(bodyHeight)},
                I18N.get(kRowLabels[rows_[i]]), label);
    if (const char* current = rowValue(rows_[i])) {
      target.text(
          fui::Rect{static_cast<int16_t>(row.x + row.width / 2), static_cast<int16_t>(middle - smallHeight / 2),
                    static_cast<int16_t>(chevronX - gap - row.x - row.width / 2), static_cast<int16_t>(smallHeight)},
          current, value);
    }
    RickyPageUi::chevron(target, fui::Rect{static_cast<int16_t>(chevronX), row.y, 24, row.height});
  }
}

void RickyStandbySettingsActivity::openPicturePicker(const std::string& folder) {
  // Back from the preview returns to the list it was opened from, not to this page.
  const auto previewDone = [this](const ActivityResult& result) {
    const auto* entry = std::get_if<FilePathResult>(&result.data);
    if (result.isCancelled && entry) {
      openPicturePicker(FsHelpers::extractFolderPath(entry->path));
      return;
    }
    requestUpdate();
  };
  if (!startActivityForResultWith<FileBrowserActivity>(
          [this, previewDone](const ActivityResult& result) {
            const auto* entry = std::get_if<FilePathResult>(&result.data);
            if (result.isCancelled || !entry ||
                !startActivityForResultWith<ImageViewerActivity>(previewDone, entry->path, true)) {
              requestUpdate();
            }
          },
          folder, FileBrowserActivity::Mode::PickWallpaper)) {
    LOG_ERR("STANDBY", "Cannot open the picture picker");
  }
}

void RickyStandbySettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount_ || optionPopup.isActive()) return;
  app.clearTapFlash();
  activeNav().selected = index + 1;
  switch (rows_[index]) {
    case Style:
      showOption(optionPopup, StrId::STR_RICKY_STANDBY_STYLE, kStyleLabels, styleIndex(),
                 [](const int chosen) { SETTINGS.rickyStandbyFace = kStyles[chosen]; });
      break;
    case ChoosePicture:
      openPicturePicker();
      return;
    case DownloadPictures:
      // Restarts to Home when it leaves after using the network, like the font downloader.
      startActivityForResultWith<RickyWallpaperDownloadActivity>([this](const ActivityResult&) { requestUpdate(); });
      return;
    case PictureFit:
      showOption(optionPopup, StrId::STR_RICKY_PICTURE_FIT, kFitLabels, SETTINGS.sleepScreenCoverMode,
                 [](const int chosen) { SETTINGS.sleepScreenCoverMode = static_cast<uint8_t>(chosen); });
      break;
    case PictureFilter:
      showOption(optionPopup, StrId::STR_RICKY_PICTURE_FILTER, kFilterLabels, SETTINGS.sleepScreenCoverFilter,
                 [](const int chosen) { SETTINGS.sleepScreenCoverFilter = static_cast<uint8_t>(chosen); });
      break;
    case StandbyInfo:
      showOption(optionPopup, StrId::STR_RICKY_STANDBY_INFO, kInfoLabels, std::min<int>(SETTINGS.standbyOverlay, 2),
                 [](const int chosen) { SETTINGS.standbyOverlay = static_cast<uint8_t>(chosen); });
      break;
    case AutoStandby:
      showOption(optionPopup, StrId::STR_RICKY_AUTO_STANDBY, kStandbyMinuteLabels,
                 std::max(0, standbyMinutesIndex()),
                 [](const int chosen) { SETTINGS.sleepTimeoutMinutes = kStandbyMinutes[chosen]; });
      break;
    case StandbyNow:
      startActivityForResultWith<StandbyActivity>([this](const ActivityResult&) { requestUpdate(); });
      return;
    case PowerOffScreen:
      showOption(optionPopup, StrId::STR_RICKY_POWER_OFF_SCREEN, kPowerOffLabels,
                 std::min<int>(SETTINGS.rickyPowerOffScreen, 2),
                 [](const int chosen) { SETTINGS.rickyPowerOffScreen = static_cast<uint8_t>(chosen); });
      break;
    case AutoPowerOff:
      showOption(optionPopup, StrId::STR_RICKY_AUTO_OFF, kAutoOffLabels, SETTINGS.rickyAutoOffIndex,
                 [](const int chosen) { SETTINGS.rickyAutoOffIndex = static_cast<uint8_t>(chosen); });
      break;
    default:
      return;
  }
  requestUpdate();
}
#endif
