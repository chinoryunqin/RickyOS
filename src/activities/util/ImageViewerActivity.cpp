#include "ImageViewerActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>
#ifdef RICKYOS_PRODUCT
#include <JpegToBmpConverter.h>
#endif

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* IMAGE_PREVIEW_PATH = "/.crosspoint/image_preview.bmp";
constexpr const char* TRANSPARENT_PREVIEW_PATH = "/.crosspoint/image_preview.transparent.bmp";
constexpr const char* SLEEP_IMAGE_PATH = "/sleep.bmp";
constexpr const char* SLEEP_IMAGE_PART_PATH = "/sleep.bmp.part";
constexpr const char* SLEEP_IMAGE_BACKUP_PATH = "/sleep.bmp.bak";
#ifdef RICKYOS_PRODUCT
// Converted previews are kept: a PNG takes seconds to convert, and the same pictures
// are opened again and again while choosing a wallpaper. Bounded by kPreviewCacheMax.
constexpr const char* PREVIEW_CACHE_DIR = "/.crosspoint/previews";
constexpr int kPreviewCacheMax = 16;

std::string previewCachePath(const std::string& source) {
  HalFile file;
  uint32_t size = 0;
  if (Storage.openFileForRead("IMAGE", source.c_str(), file)) {
    size = static_cast<uint32_t>(file.fileSize());
    file.close();
  }
  // FNV-1a over path and size: a replaced picture under the same name converts again.
  uint32_t hash = 2166136261u;
  const auto mix = [&hash](const uint8_t byte) { hash = (hash ^ byte) * 16777619u; };
  for (const char c : source) mix(static_cast<uint8_t>(c));
  for (int i = 0; i < 4; ++i) mix(static_cast<uint8_t>(size >> (i * 8)));
  char name[48];
  snprintf(name, sizeof(name), "%s/%08x.bmp", PREVIEW_CACHE_DIR, static_cast<unsigned>(hash));
  return name;
}

void prunePreviewCache() {
  HalFile dir = Storage.open(PREVIEW_CACHE_DIR);
  if (!dir || !dir.isDirectory()) return;
  std::vector<std::string> names;
  char name[64];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    name[0] = '\0';
    entry.getName(name, sizeof(name));
    entry.close();
    if (name[0] != '\0' && name[0] != '.') names.emplace_back(name);
  }
  dir.close();
  if (static_cast<int>(names.size()) < kPreviewCacheMax) return;
  // Simple and bounded: start the cache over rather than track ages.
  for (const auto& entry : names) Storage.remove((std::string(PREVIEW_CACHE_DIR) + "/" + entry).c_str());
}
#endif

constexpr StrId sleepCoverLabel() {
#ifdef RICKYOS_PRODUCT
  return StrId::STR_RICKY_SET_WALLPAPER;
#else
  return StrId::STR_SET_SLEEP_COVER;
#endif
}

Rect sleepCoverActionRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return Rect(metrics.contentSidePadding,
              renderer.getScreenHeight() - metrics.contentSidePadding - metrics.menuRowHeight,
              renderer.getScreenWidth() - metrics.contentSidePadding * 2, metrics.menuRowHeight);
}
}  // namespace

ImageViewerActivity::ImageViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                         const bool wallpaperPicker)
    : Activity("ImageViewer", renderer, mappedInput), filePath(std::move(path)), wallpaperPicker(wallpaperPicker) {}

void ImageViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) return;

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        if (FsHelpers::hasImageExtension(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
  }

  FsHelpers::sortFileList(siblingImages);

  const auto image = std::find(siblingImages.begin(), siblingImages.end(), fileName);
  if (image != siblingImages.end()) {
    currentImageIndex = static_cast<int>(image - siblingImages.begin());
  }
}

bool ImageViewerActivity::isPng() const { return FsHelpers::hasPngExtension(filePath); }

bool ImageViewerActivity::preparePreview() {
#ifdef RICKYOS_PRODUCT
  previewPath = previewCachePath(filePath);
  if (Storage.exists(previewPath.c_str())) return true;
  if (!Storage.ensureDirectoryExists(PREVIEW_CACHE_DIR)) return false;
  prunePreviewCache();
  // Written aside and renamed, so a conversion cut short is never taken for a preview.
  const std::string outPath = previewPath + ".part";
#else
  previewPath = IMAGE_PREVIEW_PATH;
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  const std::string outPath = previewPath;
#endif
  if (Storage.exists(outPath.c_str()) && !Storage.remove(outPath.c_str())) return false;

  bool prepared = false;
  {
    GfxRenderer::FrameBufferLoan loan(renderer);
    if (isPng()) {
#ifdef RICKYOS_PRODUCT
      // Read Pico's panel shows 16 grays; keep them all instead of dithering to four.
      prepared = PngToBmpConverter::pngFileToGray8BmpFile(filePath.c_str(), outPath.c_str(), true);
#else
      prepared = PngToBmpConverter::pngFileToBmpFile(filePath.c_str(), outPath.c_str(), true);
#endif
    } else {
      HalFile input, output;
      if (Storage.openFileForRead("IMAGE", filePath.c_str(), input) &&
          Storage.openFileForWrite("IMAGE", outPath, output)) {
#ifdef RICKYOS_PRODUCT
        // Read Pico's panel shows 16 grays; keep the photo's full gray range.
        prepared =
            JpegToBmpConverter::jpegFileToBmpStream(input, output, /*crop=*/false, JpegToBmpConverter::Output::Gray8);
#else
        prepared = JpegToBmpConverter::jpegFileToBmpStreamWithSize(input, output, renderer.getScreenWidth(),
                                                                   renderer.getScreenHeight(), /*crop=*/false);
#endif
        output.flush();
      }
    }
  }
#ifdef RICKYOS_PRODUCT
  prepared = prepared && Storage.rename(outPath.c_str(), previewPath.c_str());
#endif
  if (!prepared) Storage.remove(outPath.c_str());
  return prepared;
}

void ImageViewerActivity::onEnter() {
  Activity::onEnter();
  imageReady = false;
  if (!wallpaperPicker && siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
#ifdef RICKYOS_PRODUCT
  // No blank page first: the loading popup stays over the list until the picture lands
  // in one full refresh (a white screen for seconds read as a hang). The full GC16
  // commit below clears what was underneath.
#endif
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  const bool needsPreview = isPng() || FsHelpers::hasJpgExtension(filePath);
  const unsigned long tPrep = millis();
  const bool prepared = !needsPreview || preparePreview();
  LOG_DBG("IMGV", "timing: preview %lu ms", millis() - tPrep);
  const char* bitmapPath = needsPreview ? previewPath.c_str() : filePath.c_str();
  HalFile file;
  // 1. Open the file
  if (!prepared) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_IMAGE_FILE));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (Storage.openFileForRead("IMAGE", bitmapPath, file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                      currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      const auto labels = mappedInput.mapLabels(tr(STR_BACK), I18N.get(sleepCoverLabel()), (hasPrevious ? "<" : ""),
                                                (hasNext ? ">" : ""));

      GUI.fillPopupProgress(renderer, popupRect, 50);

#ifdef RICKYOS_PRODUCT
      // The 16-level pass below draws the picture itself; a B/W pass first only cost time.
      const bool native16 = bitmap.hasGreyscale() && renderer.getGrayscaleLevels() == 16;
#else
      constexpr bool native16 = false;
#endif
      renderer.clearScreen();
      if (!native16 && !renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
        renderer.clearScreen();
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return;
      }

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      if (mappedInput.hasTouch()) {
        GUI.drawActionButton(renderer, sleepCoverActionRect(renderer), I18N.get(sleepCoverLabel()));
      }
#ifdef RICKYOS_PRODUCT
      // Native 16-gray, as Standby and the sleep screen draw it. The overlay-mask path
      // also painted the Set button into its gray planes, turning it into a dark bar
      // with no readable label. Here the button is drawn in B/W and copied in.
      if (bitmap.hasGreyscale() && renderer.getGrayscaleLevels() == 16 && bitmap.rewindToData() == BmpReaderError::Ok) {
        const unsigned long tGray = millis();
        bool shown =
            renderer.beginGrayscale16() && renderer.drawBitmapGrayscale16(bitmap, x, y, pageWidth, pageHeight, 0, 0);
        LOG_DBG("IMGV", "timing: gray16 draw %lu ms", millis() - tGray);
        const unsigned long tCommit = millis();
#if !defined(SIMULATOR)
        // A clean full refresh for a picture that stays up, then the rails go off.
        if (shown) display.setNextGray16Profile(3);
#endif
        if (shown && mappedInput.hasTouch()) {
          const Rect action = sleepCoverActionRect(renderer);
          GUI.drawActionButton(renderer, action, I18N.get(sleepCoverLabel()));
          renderer.copyBwToGrayscale16(action.x, action.y, action.width, action.height);
        }
        shown = shown && renderer.commitGrayscale16();
        LOG_DBG("IMGV", "timing: gray16 commit %lu ms", millis() - tCommit);
        renderer.cancelGrayscale16();
        if (!shown) {
          LOG_ERR("BMP", "16-gray preview failed");
          renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        }
        imageReady = shown;
        return;
      }
#endif
      if (bitmap.hasGreyscale()) {
#ifdef RICKYOS_PRODUCT
        // Read Pico's bitmap renderer emits overlay masks, not UC8279 absolute planes.
        constexpr bool absolute = false;
#else
        const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
#endif
        if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return;
        if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
        bool planesReady = true;
        for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
          if (bitmap.rewindToData() != BmpReaderError::Ok) {
            LOG_ERR("BMP", "Failed to rewind bitmap for grayscale rendering");
            planesReady = false;
            break;
          }
          renderer.clearScreen(absolute ? 0xFF : 0x00);
          renderer.setRenderMode(mode);
          if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
            planesReady = false;
            break;
          }
          GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          if (mappedInput.hasTouch())
            GUI.drawActionButton(renderer, sleepCoverActionRect(renderer), I18N.get(sleepCoverLabel()));
          if (mode == GfxRenderer::GRAYSCALE_LSB) {
            renderer.copyGrayscaleLsbBuffers();
          } else {
            renderer.copyGrayscaleMsbBuffers();
          }
        }
        if (planesReady) renderer.displayGrayBuffer();

        // Rebuild the BW framebuffer for popups and subsequent differential updates.
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.clearScreen();
        if (bitmap.rewindToData() != BmpReaderError::Ok ||
            !renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
          LOG_ERR("BMP", "Failed to rewind bitmap to restore the BW framebuffer");
          renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
          planesReady = false;
        }
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        renderer.cleanupGrayscaleWithFrameBuffer();
        if (!planesReady) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        imageReady = planesReady;
      } else {
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        imageReady = true;
      }

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_IMAGE_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }

  } else {
    // Handle file open error
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void ImageViewerActivity::onExit() {
  Activity::onExit();
  imageReady = false;
#ifndef RICKYOS_PRODUCT
  if (Storage.exists(IMAGE_PREVIEW_PATH)) Storage.remove(IMAGE_PREVIEW_PATH);
#endif
  if (Storage.exists(TRANSPARENT_PREVIEW_PATH)) Storage.remove(TRANSPARENT_PREVIEW_PATH);
  renderer.clearScreen();
#ifdef RICKYOS_PRODUCT
  // Leave no gray picture under the next page (see onEnter).
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
#else
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
#endif
}

bool ImageViewerActivity::doSetSleepCover(const char* sourcePath, const bool transparent) {
  if (!imageReady) return false;
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  const char* preparedPath = sourcePath;
  bool success = true;
  if (transparent) {
    GfxRenderer::FrameBufferLoan loan(renderer);
    success = PngToBmpConverter::pngFileToTransparentBmpFile(sourcePath, TRANSPARENT_PREVIEW_PATH, true);
    if (success) preparedPath = TRANSPARENT_PREVIEW_PATH;
  }

  Storage.remove(SLEEP_IMAGE_PART_PATH);
  if (success) {
    HalFile inFile, outFile;
    if (Storage.openFileForRead("IMAGE", preparedPath, inFile) &&
        Storage.openFileForWrite("IMAGE", SLEEP_IMAGE_PART_PATH, outFile)) {
      const uint64_t expected = inFile.fileSize64();
      uint64_t copied = 0;
      // A full-screen 16-gray BMP is ~400 KB: 128-byte chunks took over ten seconds.
      static char buffer[4096];
      int bytesRead;
      while ((bytesRead = inFile.read(buffer, sizeof(buffer))) > 0) {
        if (outFile.write(buffer, bytesRead) != bytesRead) {
          success = false;
          break;
        }
        copied += bytesRead;
      }
      outFile.flush();
      success = success && copied == expected;
    } else {
      success = false;
    }
  }

  if (success) {
    Storage.remove(SLEEP_IMAGE_BACKUP_PATH);
    const bool hadPrevious = Storage.exists(SLEEP_IMAGE_PATH);
    if ((hadPrevious && !Storage.rename(SLEEP_IMAGE_PATH, SLEEP_IMAGE_BACKUP_PATH)) ||
        !Storage.rename(SLEEP_IMAGE_PART_PATH, SLEEP_IMAGE_PATH)) {
      if (hadPrevious && !Storage.exists(SLEEP_IMAGE_PATH)) {
        Storage.rename(SLEEP_IMAGE_BACKUP_PATH, SLEEP_IMAGE_PATH);
      }
      success = false;
    }
  }

  if (success) {
    const uint8_t previousMode = SETTINGS.sleepScreen;
    SETTINGS.sleepScreen = transparent ? CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT
                                       : CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    if (!SETTINGS.saveToFile()) {
      SETTINGS.sleepScreen = previousMode;
      (void)SETTINGS.saveToFile();
      Storage.remove(SLEEP_IMAGE_PATH);
      if (Storage.exists(SLEEP_IMAGE_BACKUP_PATH)) {
        Storage.rename(SLEEP_IMAGE_BACKUP_PATH, SLEEP_IMAGE_PATH);
      }
      success = false;
    }
  }

  Storage.remove(SLEEP_IMAGE_PART_PATH);
  if (success) Storage.remove(SLEEP_IMAGE_BACKUP_PATH);
  GUI.drawPopup(renderer, success ? tr(STR_DONE) : tr(STR_FAILED_LOWER));
  delay(1000);
  return success;
}

void ImageViewerActivity::showSleepCoverOptions() {
  if (!imageReady) return;
  if (!isPng()) {
    // Picking a wallpaper ends here: return to the page that asked, which shows the new picture.
    if (doSetSleepCover(FsHelpers::hasJpgExtension(filePath) ? previewPath.c_str() : filePath.c_str(), false) &&
        wallpaperPicker)
      finish();
    return;
  }

  static constexpr StrId options[] = {StrId::STR_NORMAL, StrId::STR_TRANSPARENT};
  static constexpr int optionCount = sizeof(options) / sizeof(options[0]);
  sleepCoverPopup.show(sleepCoverLabel(), options, optionCount, 0, [this](const int index) {
    if (doSetSleepCover(index == 1 ? filePath.c_str() : previewPath.c_str(), index == 1) && wallpaperPicker) finish();
  });
  requestUpdate();
}

void ImageViewerActivity::render(RenderLock&&) { sleepCoverPopup.processRender(renderer, mappedInput); }

void ImageViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  if (sleepCoverPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (!sleepCoverPopup.isActive() && !activityManager.isSwitchPending()) onEnter();
    return;
  }

  auto openSibling = [this](const int delta) {
    if (currentImageIndex < 0) {
      return false;
    }
    const int nextIndex = currentImageIndex + delta;
    if (siblingImages.size() <= 1 || nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) {
      return false;
    }
    currentImageIndex = nextIndex;
    std::string dirPath = FsHelpers::extractFolderPath(filePath);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + siblingImages[currentImageIndex];
    onEnter();
    return true;
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (wallpaperPicker) {
      // Nothing was set: the picker reopens its list, where the reader came from.
      ActivityResult cancelled;
      cancelled.isCancelled = true;
      cancelled.data = FilePathResult{filePath};
      setResult(std::move(cancelled));
      finish();
    } else
      activityManager.goToFileBrowser(filePath);
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    openSibling(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    openSibling(-1);
    return;
  }

  if (mappedInput.hasTouch()) {
    const Rect sleepCoverAction = sleepCoverActionRect(renderer);
    if (mappedInput.wasTapInRect(sleepCoverAction.x, sleepCoverAction.y, sleepCoverAction.width,
                                 sleepCoverAction.height)) {
      showSleepCoverOptions();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showSleepCoverOptions();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openSibling(1);
    return;
  }
}
