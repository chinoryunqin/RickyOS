#ifdef RICKYOS_PRODUCT
#include "WallpaperFace.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Same file the Settings picker installs and the sleep screen shows.
constexpr char kPicture[] = "/sleep.bmp";
constexpr int kCornerMargin = 28;
constexpr int kCornerPadding = 20;
constexpr int kCornerRadius = 18;
constexpr int kTimeFont = NOTOSANS_18_FONT_ID;  // largest built-in face; drawn bold
constexpr int kDateFont = UI_10_FONT_ID;

bool showsTime() { return SETTINGS.standbyOverlay == CrossPointSettings::STANDBY_OVERLAY_TIME; }
bool showsCorner() { return SETTINGS.standbyOverlay != CrossPointSettings::STANDBY_OVERLAY_NONE; }

// Fit the picture inside the screen, centered; smaller pictures are not enlarged.
void placeBitmap(const Bitmap& bitmap, const int screenWidth, const int screenHeight, int& x, int& y) {
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  if (width > screenWidth || height > screenHeight) {
    const float ratio = static_cast<float>(width) / static_cast<float>(height);
    const float screenRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    if (ratio > screenRatio) {
      x = 0;
      y = static_cast<int>(std::lround((screenHeight - screenWidth / ratio) / 2));
    } else {
      x = static_cast<int>(std::lround((screenWidth - screenHeight * ratio) / 2));
      y = 0;
    }
  } else {
    x = (screenWidth - width) / 2;
    y = (screenHeight - height) / 2;
  }
}
}  // namespace

void WallpaperFace::onEnter() {
  hasPicture_ = Storage.exists(kPicture);
  lastMinute_ = -1;
  lastDay_ = -1;
}

StandbyFace::TickResult WallpaperFace::tick() {
  if (!hasPicture_ || !showsCorner()) return TickResult::None;
  std::tm local{};
  if (!halClock.localTime(local)) return TickResult::None;
  const int32_t minute = local.tm_hour * 60 + local.tm_min;
  const bool dayChanged = local.tm_yday != lastDay_;
  if (!showsTime() && !dayChanged) return TickResult::None;
  if (showsTime() && minute == lastMinute_ && !dayChanged) return TickResult::None;
  return TickResult::Redraw;
}

uint32_t WallpaperFace::secondsUntilNextWake() const {
  const time_t now = time(nullptr);
  if (showsTime()) {
    const uint32_t second = static_cast<uint32_t>(now % 60);
    return second == 0 ? 60u : 60u - second;
  }
  if (showsCorner()) {
    std::tm local{};
    if (halClock.localTime(local)) {
      return static_cast<uint32_t>(std::max(1, 86400 - (local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec)));
    }
  }
  return 3600u;
}

bool WallpaperFace::wantsClock() const { return showsCorner(); }

StandbyFace::PictureAction WallpaperFace::pictureActionAt(const int x, const int y) const {
  const auto inside = [x, y](const Rect& r) { return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height; };
  if (hasPicture_) return PictureAction::None;
  if (inside(downloadButton_)) return PictureAction::Download;
  if (inside(chooseButton_)) return PictureAction::Choose;
  return PictureAction::None;
}

void WallpaperFace::drawCorner(GfxRenderer& renderer, const Rect& viewport, const bool intoGray) const {
  if (!showsCorner()) return;
  std::tm local{};
  if (!halClock.localTime(local)) return;  // nothing to show until the clock is set

  static constexpr StrId weekdays[] = {
      StrId::STR_CAL_WEEKDAY_SUN, StrId::STR_CAL_WEEKDAY_MON, StrId::STR_CAL_WEEKDAY_TUE, StrId::STR_CAL_WEEKDAY_WED,
      StrId::STR_CAL_WEEKDAY_THU, StrId::STR_CAL_WEEKDAY_FRI, StrId::STR_CAL_WEEKDAY_SAT};
  char date[48];
  snprintf(date, sizeof(date), tr(STR_RICKY_HOME_DATE), static_cast<unsigned>(local.tm_mon + 1),
           static_cast<unsigned>(local.tm_mday), I18N.get(weekdays[std::clamp(local.tm_wday, 0, 6)]));
  char clock[8];
  snprintf(clock, sizeof(clock), "%02d:%02d", local.tm_hour, local.tm_min);

  const bool time = showsTime();
  const int timeHeight = time ? renderer.getLineHeight(kTimeFont) : 0;
  const int dateHeight = renderer.getLineHeight(kDateFont);
  // Noto Sans digits are tabular, so "00:00" sizes every minute: a corner-only
  // update always covers the previous minute exactly.
  const int textWidth = std::max(renderer.getTextWidth(kDateFont, date),
                                 time ? renderer.getTextWidth(kTimeFont, "00:00", EpdFontFamily::BOLD) : 0);
  const int width = textWidth + 2 * kCornerPadding;
  const int height = 2 * kCornerPadding + timeHeight + dateHeight;
  const int x = viewport.x + viewport.width - kCornerMargin - width;
  const int y = viewport.y + viewport.height - kCornerMargin - height;

  renderer.fillRoundedRect(x, y, width, height, kCornerRadius, Color::White);
  renderer.drawRoundedRect(x, y, width, height, 2, kCornerRadius, true);
  int textY = y + kCornerPadding;
  if (time) {
    renderer.drawText(kTimeFont, x + kCornerPadding, textY, clock, true, EpdFontFamily::BOLD);
    textY += timeHeight;
  }
  renderer.drawText(kDateFont, x + kCornerPadding, textY, date, true);
  if (intoGray) renderer.copyBwToGrayscale16(x, y, width, height, kCornerRadius);
}

bool WallpaperFace::renderNative(GfxRenderer& renderer, const Rect& viewport) {
  if (!hasPicture_) return false;
  std::tm local{};
  const bool clock = halClock.localTime(local);

  // Every update redraws the whole 16-gray frame. This panel has no partial
  // refresh: a B/W refresh after a 16-gray frame drives every gray pixel to black
  // or white, which collapsed the picture after the first minute.
  if (renderer.getGrayscaleLevels() != 16) return false;
  HalFile file;
  if (!Storage.openFileForRead("STANDBY", kPicture, file)) {
    hasPicture_ = false;
    return false;
  }
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("STANDBY", "Unreadable standby picture");
    hasPicture_ = false;
    return false;
  }
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  int x = 0, y = 0;
  placeBitmap(bitmap, screenWidth, screenHeight, x, y);
  bool shown =
      renderer.beginGrayscale16() && renderer.drawBitmapGrayscale16(bitmap, x, y, screenWidth, screenHeight, 0, 0);
  if (shown) {
    drawCorner(renderer, viewport, true);
    shown = renderer.commitGrayscale16();
  }
  renderer.cancelGrayscale16();
  if (!shown) {
    LOG_ERR("STANDBY", "16-gray standby picture failed");
    return false;
  }
  if (clock) {
    lastMinute_ = local.tm_hour * 60 + local.tm_min;
    lastDay_ = local.tm_yday;
  }
  return true;
}

// B/W path: the empty state, and the picture on panels without native 16-gray.
void WallpaperFace::render(GfxRenderer& renderer, const Rect& viewport) {
  if (!hasPicture_) {
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int centerY = viewport.y + viewport.height / 2;
    UITheme::drawCenteredText(renderer, viewport, UI_12_FONT_ID, centerY - lineHeight * 3, tr(STR_RICKY_STANDBY_EMPTY));
    const int buttonHeight = renderer.getLineHeight(UI_10_FONT_ID) + 28;
    const int buttonWidth = std::max(renderer.getTextWidth(UI_10_FONT_ID, tr(STR_RICKY_SELECT_WALLPAPER)),
                                     renderer.getTextWidth(UI_10_FONT_ID, tr(STR_RICKY_WALLPAPER_DOWNLOAD))) +
                            72;
    const int buttonX = viewport.x + (viewport.width - buttonWidth) / 2;
    chooseButton_ = Rect{buttonX, centerY - buttonHeight, buttonWidth, buttonHeight};
    downloadButton_ = Rect{buttonX, centerY + 20, buttonWidth, buttonHeight};
    // The primary action is filled; downloading the defaults is the outlined one.
    renderer.fillRoundedRect(chooseButton_.x, chooseButton_.y, buttonWidth, buttonHeight, buttonHeight / 2,
                             Color::Black);
    renderer.drawRoundedRect(downloadButton_.x, downloadButton_.y, buttonWidth, buttonHeight, 2, buttonHeight / 2,
                             true);
    const auto label = [&](const Rect& button, const char* text, const bool ink) {
      renderer.drawText(UI_10_FONT_ID, button.x + (button.width - renderer.getTextWidth(UI_10_FONT_ID, text)) / 2,
                        button.y + 14, text, ink);
    };
    label(chooseButton_, tr(STR_RICKY_SELECT_WALLPAPER), false);
    label(downloadButton_, tr(STR_RICKY_WALLPAPER_DOWNLOAD), true);
    return;
  }
  HalFile file;
  if (!Storage.openFileForRead("STANDBY", kPicture, file)) return;
  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return;
  int x = 0, y = 0;
  placeBitmap(bitmap, renderer.getScreenWidth(), renderer.getScreenHeight(), x, y);
  renderer.drawBitmap(bitmap, x, y, renderer.getScreenWidth(), renderer.getScreenHeight(), 0, 0);
  drawCorner(renderer, viewport, false);
  std::tm local{};
  if (halClock.localTime(local)) {
    lastMinute_ = local.tm_hour * 60 + local.tm_min;
    lastDay_ = local.tm_yday;
  }
}
#endif
