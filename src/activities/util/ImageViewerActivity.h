#pragma once

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ImageViewerActivity final : public Activity {
 public:
  ImageViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                      bool wallpaperPicker = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  void loadSiblingImages();
  bool isPng() const;
  bool preparePreview();
  void doSetSleepCover(const char* sourcePath, bool transparent);
  void showSleepCoverOptions();

  std::string filePath;
  bool wallpaperPicker = false;
  bool imageReady = false;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  OptionPopup sleepCoverPopup;
};
