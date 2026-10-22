#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ImageViewerActivity final : public Activity {
 public:
  enum class WallpaperTarget : uint8_t { Standby, PowerOff };
  ImageViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                      bool wallpaperPicker = false, bool stickerPicker = false,
                      WallpaperTarget wallpaperTarget = WallpaperTarget::Standby);

  // What the file holds, read from its first bytes: saved pictures often carry the wrong
  // extension (a JPEG named .png), and a decoder chosen by name then rejects them.
  enum class ImageKind : uint8_t { Bmp, Png, Jpeg };

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  void loadSiblingImages();
  bool isPng() const;
  bool isJpeg() const;
  bool preparePreview();
  bool doSetSleepCover(const char* sourcePath, bool transparent);
  bool doSetSticker();
  void showSleepCoverOptions();

  std::string filePath;
  std::string previewPath;  // the converted BMP shown for a PNG/JPEG
  ImageKind imageKind = ImageKind::Bmp;
  bool wallpaperPicker = false;
  bool stickerPicker = false;
  WallpaperTarget wallpaperTarget = WallpaperTarget::Standby;
  bool imageReady = false;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  OptionPopup sleepCoverPopup;
};
