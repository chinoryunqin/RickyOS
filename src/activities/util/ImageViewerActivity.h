#pragma once

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ImageViewerActivity final : public Activity {
 public:
  ImageViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                      bool wallpaperPicker = false, bool stickerPicker = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  void loadSiblingImages();
  bool isPng() const;
  bool preparePreview();
  bool doSetSleepCover(const char* sourcePath, bool transparent);
  bool doSetSticker();
  void showSleepCoverOptions();

  std::string filePath;
  std::string previewPath;  // the converted BMP shown for a PNG/JPEG
  bool wallpaperPicker = false;
  bool stickerPicker = false;
  bool imageReady = false;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  OptionPopup sleepCoverPopup;
};
