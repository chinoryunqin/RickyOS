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
constexpr char kPicture[] = "/sleep.bmp";

// Lock-screen modes in the order people pick them; the picture first.
constexpr uint8_t kModes[] = {CrossPointSettings::CUSTOM,       CrossPointSettings::COVER,
                              CrossPointSettings::COVER_CUSTOM, CrossPointSettings::QUICK_RESUME,
                              CrossPointSettings::LIGHT,        CrossPointSettings::DARK,
                              CrossPointSettings::BLANK,        CrossPointSettings::TRANSPARENT};
constexpr StrId kModeLabels[] = {StrId::STR_RICKY_SLEEP_CUSTOM,       StrId::STR_RICKY_SLEEP_COVER,
                                 StrId::STR_RICKY_SLEEP_COVER_CUSTOM, StrId::STR_RICKY_SLEEP_QUICK,
                                 StrId::STR_RICKY_SLEEP_LIGHT,        StrId::STR_RICKY_SLEEP_DARK,
                                 StrId::STR_RICKY_SLEEP_BLANK,        StrId::STR_RICKY_SLEEP_OVERLAY};
static_assert(std::size(kModes) == std::size(kModeLabels));
static_assert(std::size(kModes) == CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
constexpr StrId kInfoLabels[] = {StrId::STR_RICKY_STANDBY_INFO_NONE, StrId::STR_RICKY_STANDBY_INFO_DATE,
                                 StrId::STR_RICKY_STANDBY_INFO_TIME};
static_assert(std::size(kInfoLabels) == CrossPointSettings::STANDBY_OVERLAY_COUNT);
constexpr StrId kStyleLabels[] = {StrId::STR_RICKY_STANDBY_STYLE_PICTURE, StrId::STR_RICKY_STANDBY_STYLE_CALENDAR};
static_assert(std::size(kStyleLabels) == CrossPointSettings::RICKY_STANDBY_FACE_COUNT);

int modeIndex() {
  for (size_t i = 0; i < std::size(kModes); ++i)
    if (kModes[i] == SETTINGS.sleepScreen) return static_cast<int>(i);
  return 0;
}
}  // namespace

const char* RickyStandbySettingsActivity::headerTitle() const { return tr(STR_STANDBY_TITLE); }

bool RickyStandbySettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void RickyStandbySettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

// A small 9:16 picture of what the sleeping screen shows.
void RickyStandbySettingsActivity::drawPreview(const Rect& box) const {
  const bool picture = SETTINGS.sleepScreen == CrossPointSettings::CUSTOM ||
                       SETTINGS.sleepScreen == CrossPointSettings::COVER_CUSTOM ||
                       SETTINGS.sleepScreen == CrossPointSettings::TRANSPARENT;
  if (SETTINGS.sleepScreen == CrossPointSettings::DARK) {
    renderer.fillRoundedRect(box.x, box.y, box.width, box.height, 10, Color::Black);
    return;
  }
  renderer.drawRoundedRect(box.x, box.y, box.width, box.height, 2, 10, true);
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
  // No picture to show: name the mode (or the missing picture) inside the frame.
  const char* label = picture ? tr(STR_RICKY_STANDBY_EMPTY) : I18N.get(kModeLabels[modeIndex()]);
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  UITheme::drawCenteredText(renderer, box, SMALL_FONT_ID, box.y + (box.height - lineHeight) / 2, label);
}

void RickyStandbySettingsActivity::buildScreen(UiScreen& screen) {
  const auto bounds = pageContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(bounds.y), 0,
                  static_cast<int16_t>(renderer.getScreenHeight() - bounds.y - bounds.height), 0});
  const auto& theme = screen.theme();
  auto& target = screen.target();
  screen.insetContent(fui::Insets{static_cast<int16_t>(theme.spaceLg), theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int gap = std::max<int>(12, theme.spaceSm);
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  const int selected = RickyPageUi::syncNav(nav, listCount());
  const bool focus = showMainTabContentSelection();
  RickyPageUi::Bold bold(renderer, 1);

  const int rowHeight = bodyHeight + gap * 2;
  const int rowsHeight = rowHeight * RowCount + gap * (RowCount - 1);
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
  const StrId labels[RowCount] = {StrId::STR_RICKY_STANDBY_SCREEN,     StrId::STR_RICKY_SELECT_WALLPAPER,
                                  StrId::STR_RICKY_WALLPAPER_DOWNLOAD, StrId::STR_RICKY_STANDBY_STYLE,
                                  StrId::STR_RICKY_STANDBY_INFO,       StrId::STR_RICKY_STANDBY_NOW};
  for (int i = 0; i < RowCount; ++i) {
    const auto row = screen.takeTop(rowHeight, i + 1 < RowCount ? gap : 0);
    RickyPageUi::card(target, row, focus && selected == i);
    screen.frame().hit(row, ACTION_ROW, i, fui::InputTouch);
    const int middle = row.y + row.height / 2;
    const int textX = row.x + gap * 2;
    const int chevronX = row.right() - 24 - gap;
    target.text(fui::Rect{static_cast<int16_t>(textX), static_cast<int16_t>(middle - bodyHeight / 2),
                          static_cast<int16_t>(row.width / 2), static_cast<int16_t>(bodyHeight)},
                I18N.get(labels[i]), label);
    const char* current = i == ScreenMode ? I18N.get(kModeLabels[modeIndex()])
                          : i == StandbyStyle
                              ? I18N.get(kStyleLabels[SETTINGS.rickyStandbyFace % std::size(kStyleLabels)])
                          : i == StandbyInfo ? I18N.get(kInfoLabels[std::min<int>(SETTINGS.standbyOverlay, 2)])
                                             : nullptr;
    if (current) {
      const int smallHeight = target.lineHeight(theme.smallText.font);
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
  if (index < 0 || index >= listCount() || optionPopup.isActive()) return;
  app.clearTapFlash();
  nav.selected = index;
  switch (index) {
    case ScreenMode:
      optionPopup.show(StrId::STR_RICKY_STANDBY_SCREEN, kModeLabels, static_cast<int>(std::size(kModeLabels)),
                       modeIndex(), [](const int chosen) {
                         SETTINGS.sleepScreen = kModes[chosen];
                         SETTINGS.saveToFile();
                       });
      break;
    case ChoosePicture:
      openPicturePicker();
      return;
    case DownloadPictures:
      // Restarts to Home when it leaves after using the network, like the font downloader.
      startActivityForResultWith<RickyWallpaperDownloadActivity>([this](const ActivityResult&) { requestUpdate(); });
      return;
    case StandbyStyle:
      optionPopup.show(StrId::STR_RICKY_STANDBY_STYLE, kStyleLabels, static_cast<int>(std::size(kStyleLabels)),
                       SETTINGS.rickyStandbyFace % std::size(kStyleLabels), [](const int chosen) {
                         SETTINGS.rickyStandbyFace = static_cast<uint8_t>(chosen);
                         SETTINGS.saveToFile();
                       });
      break;
    case StandbyInfo:
      optionPopup.show(StrId::STR_RICKY_STANDBY_INFO, kInfoLabels, static_cast<int>(std::size(kInfoLabels)),
                       std::min<int>(SETTINGS.standbyOverlay, 2), [](const int chosen) {
                         SETTINGS.standbyOverlay = static_cast<uint8_t>(chosen);
                         SETTINGS.saveToFile();
                       });
      break;
    case StandbyNow:
      startActivityForResultWith<StandbyActivity>([this](const ActivityResult&) { requestUpdate(); });
      return;
    default:
      return;
  }
  requestUpdate();
}
#endif
