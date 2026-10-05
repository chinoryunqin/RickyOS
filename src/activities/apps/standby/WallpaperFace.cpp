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
  pictureShown_ = false;
  fullRedraw_ = true;
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
  // A new day can widen the date; the hour cleans the corner's partial-refresh ghosting.
  if (dayChanged || local.tm_min == 0) fullRedraw_ = true;
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

  if (pictureShown_ && !fullRedraw_) {
    // The B/W proxy still matches the panel outside the corner, so a partial
    // refresh changes the corner only and the picture keeps its grays.
    drawCorner(renderer, viewport, false);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
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
    pictureShown_ = true;
    fullRedraw_ = false;
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
    UITheme::drawCenteredText(renderer, viewport, UI_12_FONT_ID, centerY - lineHeight * 2, tr(STR_RICKY_STANDBY_EMPTY));
    const char* action = tr(STR_RICKY_SELECT_WALLPAPER);
    const int buttonWidth = renderer.getTextWidth(UI_10_FONT_ID, action) + 56;
    const int buttonHeight = renderer.getLineHeight(UI_10_FONT_ID) + 28;
    const int buttonX = viewport.x + (viewport.width - buttonWidth) / 2;
    const int buttonY = centerY;
    renderer.drawRoundedRect(buttonX, buttonY, buttonWidth, buttonHeight, 2, buttonHeight / 2, true);
    renderer.drawText(UI_10_FONT_ID, buttonX + 28, buttonY + 14, action, true);
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
