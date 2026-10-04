#include "RickyProfile.h"
#ifdef RICKYOS_PRODUCT
#include <Bitmap.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <PngToBmpConverter.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "RickyBrandMark.h"
#include "RickyPageUi.h"
#include "UITheme.h"
#include "util/TimeUtils.h"

namespace RickyProfile {
namespace {
bool validAvatarBitmap(HalFile& file, Bitmap& bitmap) {
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) return false;
  const size_t offset = file.position();
  const size_t bytes = static_cast<size_t>(bitmap.getRowBytes()) * bitmap.getHeight();
  return offset <= file.fileSize() && bytes <= file.fileSize() - offset;
}
}  // namespace
const char* nickname() { return SETTINGS.rickyNickname[0] ? SETTINGS.rickyNickname : tr(STR_RICKY_PROFILE_DEFAULT); }

const char* homePhrase() { return SETTINGS.rickyHomePhrase[0] ? SETTINGS.rickyHomePhrase : tr(STR_RICKY_HOME_PHRASE); }

bool setHomePhrase(const std::string& text) {
  // The keyboard limits bytes, not glyphs. Reject oversized/control-containing
  // input instead of truncating inside a UTF-8 codepoint; no new heap buffer.
  if (text.size() >= sizeof(SETTINGS.rickyHomePhrase)) return false;
  for (const unsigned char c : text) {
    if (c < 0x20 || c == 0x7f) return false;
  }
  if (text == SETTINGS.rickyHomePhrase) return true;
  char previous[sizeof(SETTINGS.rickyHomePhrase)];
  memcpy(previous, SETTINGS.rickyHomePhrase, sizeof(previous));
  memcpy(SETTINGS.rickyHomePhrase, text.c_str(), text.size() + 1);
  if (SETTINGS.saveToFile()) return true;
  memcpy(SETTINGS.rickyHomePhrase, previous, sizeof(previous));
  return false;
}

void drawAvatar(const GfxRenderer& renderer, const Rect& rect) {
  const int size = std::max(0, std::min(rect.width, rect.height));
  if (!size) return;
  bool painted = false;
  if (SETTINGS.rickyAvatarPath[0]) {
    HalFile file;
    if (Storage.openFileForRead("PROFILE", SETTINGS.rickyAvatarPath, file)) {
      // Bitmap already owns bounded reusable row scratch, not a full image.
      Bitmap bitmap(file);
      if (validAvatarBitmap(file, bitmap)) {
        painted = renderer.drawBitmapCropToFill(bitmap, rect.x, rect.y, size, size);
      }
    }
  }
  if (!painted) {
    // Approved portrait/dog badge as the initial avatar; changing this personal
    // image never replaces the immutable full boot/standby brand mark. The
    // badge's own ring is the avatar edge, so no second outline is drawn.
    RickyBrandMark::drawBadge(renderer, Rect{rect.x, rect.y, size, size});
    renderer.maskRoundedRectOutsideCorners(rect.x, rect.y, size, size, size / 2);
    return;
  }
  // Imported photos get one even ring, scaled with the avatar.
  renderer.maskRoundedRectOutsideCorners(rect.x, rect.y, size, size, size / 2);
  renderer.drawRoundedRect(rect.x, rect.y, size, size, std::max(2, size / 48), size / 2, true);
}

void drawCard(UiScreen& screen, const GfxRenderer& renderer, freeink::ui::Rect rect, freeink::ui::ActionId action,
              int value) {
  namespace fui = freeink::ui;
  const auto& theme = screen.theme();
  auto& target = screen.target();
  const int gap = std::max<int>(12, theme.spaceSm);
  const int pad = gap + gap / 2;
  RickyPageUi::card(target, rect, false);
  const int size = std::max(0, rect.height - pad * 2);
  drawAvatar(renderer, Rect{rect.x + pad, rect.y + pad, size, size});
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  const int smallHeight = target.lineHeight(theme.smallText.font);
  const int x = rect.x + pad * 2 + size;
  const int width = rect.right() - x - pad - 24;
  const int top = rect.y + (rect.height - bodyHeight - smallHeight - gap / 2) / 2;
  auto name = theme.bodyText;
  name.bold = true;
  name.maxLines = 1;
  {
    RickyPageUi::Bold bold(renderer, 1);
    target.text(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(top), static_cast<int16_t>(width),
                          static_cast<int16_t>(bodyHeight)},
                nickname(), name);
  }
  auto detail = theme.smallText;
  detail.maxLines = 1;
  target.text(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(top + bodyHeight + gap / 2),
                        static_cast<int16_t>(width), static_cast<int16_t>(smallHeight)},
              tr(STR_RICKY_PROFILE), detail);
  RickyPageUi::chevron(target, fui::Rect{static_cast<int16_t>(rect.right() - 24 - pad), rect.y, 24, rect.height});
  if (action != fui::NO_ACTION) screen.frame().hit(rect, action, static_cast<int16_t>(value), fui::InputTouch);
}

bool importAvatar(const std::string& path) {
  if (path.size() > 512 || path.empty() || path.front() != '/') return false;
  const bool png = FsHelpers::hasPngExtension(path);
  const bool jpg = FsHelpers::hasJpgExtension(path);
  if (!png && !jpg && !FsHelpers::hasBmpExtension(path)) return false;
  // Alternate private slots: failure never truncates the currently used avatar.
  constexpr const char* slotA = "/.crosspoint/ricky-avatar-a.bmp";
  constexpr const char* slotB = "/.crosspoint/ricky-avatar-b.bmp";
  const char* target = strcmp(SETTINGS.rickyAvatarPath, slotA) == 0 ? slotB : slotA;
  if (path == target) return false;
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  bool ok = false;
  {
    HalFile input, output;
    if (!Storage.openFileForRead("PROFILE", path, input) || input.isDirectory() || input.fileSize() == 0 ||
        input.fileSize() > 16 * 1024 * 1024)
      return false;
    if (!png && !jpg) {
      Bitmap bitmap(input);
      if (!validAvatarBitmap(input, bitmap) || input.fileSize() > 2 * 1024 * 1024 || !input.seek(0)) return false;
    }
    if (!Storage.openFileForWrite("PROFILE", target, output)) return false;
    if (png) {
      ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(input, output, 128, 128);
    } else if (jpg) {
      ok = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(input, output, 128, 128);
    } else {
      // Small stack chunk instead of a heap copy of the user's original image.
      uint8_t chunk[512];
      size_t remaining = input.fileSize();
      ok = true;
      while (remaining) {
        const size_t count = std::min(remaining, sizeof(chunk));
        if (input.read(chunk, count) != static_cast<int>(count) || output.write(chunk, count) != count) {
          ok = false;
          break;
        }
        remaining -= count;
      }
    }
    output.flush();
  }
  if (!ok) {
    LOG_ERR("PROFILE", "avatar import failed");
    return false;
  }
  {
    HalFile file;
    if (!Storage.openFileForRead("PROFILE", target, file)) return false;
    Bitmap bitmap(file);
    if (!validAvatarBitmap(file, bitmap)) return false;
  }
  char previous[sizeof(SETTINGS.rickyAvatarPath)];
  memcpy(previous, SETTINGS.rickyAvatarPath, sizeof(previous));
  strncpy(SETTINGS.rickyAvatarPath, target, sizeof(SETTINGS.rickyAvatarPath) - 1);
  SETTINGS.rickyAvatarPath[sizeof(SETTINGS.rickyAvatarPath) - 1] = '\0';
  if (SETTINGS.saveToFile()) return true;
  memcpy(SETTINGS.rickyAvatarPath, previous, sizeof(previous));
  return false;
}
}  // namespace RickyProfile
#endif
