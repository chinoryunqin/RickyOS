#include "MetalioCameraActivity.h"
#if FREEINK_DEVICE_METALIO_EINK4
#include <HalDisplay.h>
#include <I18n.h>
#include <JPEGDEC.h>

#include <algorithm>
#include <cstring>

#include "CameraPreviewRaster.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "standby/StandbyGalleryActivity.h"
#include "util/ScreenshotUtil.h"

MetalioCameraActivity::MetalioCameraActivity(GfxRenderer& renderer, MappedInputManager& input, bool diagnostic)
    : Activity("MetalioCamera", renderer, input), diagnostic_(diagnostic) {}
MetalioCameraActivity::~MetalioCameraActivity() = default;
Rect MetalioCameraActivity::viewRect() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int side = std::max(16, m.contentSidePadding);
  const int top = m.topPadding + m.headerHeight + 24;
  return Rect(side, top, (renderer.getScreenWidth() - 2 * side) & ~7, std::max(64, actionRect(0).y - top - 70));
}
Rect MetalioCameraActivity::actionRect(int index) const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int side = std::max(16, m.contentSidePadding), gap = 8;
  const int width = (renderer.getScreenWidth() - 2 * side - gap * 2) / 3;
  return Rect(side + index * (width + gap), renderer.getScreenHeight() - m.buttonHintsHeight - m.menuRowHeight - 24,
              width, m.menuRowHeight);
}
void MetalioCameraActivity::onEnter() {
  Activity::onEnter();
  const auto area = viewRect();
  width_ = area.width;
  maxHeight_ = area.height;
  bitmap_ = HalMemory::allocatePsram(static_cast<size_t>(width_ / 8) * maxHeight_);
  // JPEGDEC owns ~22 KiB of decode state: heap lifetime, never a task-stack local.
  decoder_ = makeUniqueNoThrow<JPEGDEC>();
  if (!bitmap_ || !decoder_) {
    LOG_ERR("CAMUI", "Preview allocation failed");
    paused_ = true;
  }
  shown_ = HalCamera::snapshot();
  nextPreview_ = millis() + 500;
  requestUpdate();
}
void MetalioCameraActivity::onExit() {
  HalCamera::leave();
  bitmap_.reset();
  decoder_.reset();
  Activity::onExit();
}
int MetalioCameraActivity::decodedBlock(JPEGDRAW* block) {
  auto* self = static_cast<MetalioCameraActivity*>(block->pUser);
  if (!self || !self->bitmap_) return 0;
  cameraPreview::grayBlock({self->bitmap_.get(), self->width_, self->height_, self->sourceWidth_, self->sourceHeight_},
                           reinterpret_cast<const uint8_t*>(block->pPixels), block->x, block->y, block->iWidth,
                           block->iWidthUsed, block->iHeight);
  return 1;
}
bool MetalioCameraActivity::decodeFrame() {
  const auto frame = HalCamera::frame();
  if (!frame.data || !frame.size || !decoder_ || !bitmap_) return false;
  if (!decoder_->openRAM(const_cast<uint8_t*>(frame.data), static_cast<int>(frame.size), decodedBlock)) return false;
  int scale = 1;
  while (scale < 8 && (decoder_->getWidth() / scale > width_ * 2 || decoder_->getHeight() / scale > maxHeight_ * 2))
    scale *= 2;
  sourceWidth_ = (decoder_->getWidth() + scale - 1) / scale;
  sourceHeight_ = (decoder_->getHeight() + scale - 1) / scale;
  height_ = std::max(1, std::min(maxHeight_, width_ * sourceHeight_ / sourceWidth_));
  memset(bitmap_.get(), 0, static_cast<size_t>(width_ / 8) * maxHeight_);
  decoder_->setUserPointer(this);
  decoder_->setPixelType(EIGHT_BIT_GRAYSCALE);
  const int ok = decoder_->decode(0, 0, scale == 1 ? 0 : scale);
  decoder_->close();
  LOG_INF("CAMUI", "Preview decoded=%d input=%ux%u output=%dx%d", ok, frame.width, frame.height, width_, height_);
  return ok != 0;
}
void MetalioCameraActivity::activate(int index) {
  if (index == 0) {
    // Freeze the shown frame; save that exact JPEG after an in-flight preview finishes.
    paused_ = true;
    if (imageReady_) saveQueued_ = true;
  } else if (index == 1) {
    paused_ = !paused_;
    if (!imageReady_ || shown_.state == HalCamera::State::Error) paused_ = false;
    nextPreview_ = millis();
  } else if (index == 2 && !HalCamera::busy()) {
    paused_ = true;
    startActivityForResultWith<StandbyGalleryActivity>([](const ActivityResult&) {}, StandbyPictures::Target::Standby,
                                                       StandbyGalleryActivity::Mode::Browse);
  }
  requestUpdate();
}
void MetalioCameraActivity::loop() {
  if (waitRelease_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.isPressed(MappedInputManager::Button::Back))
      return;
    waitRelease_ = false;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !HalCamera::busy()) {
    finish();
    return;
  }
  for (int i = 0; i < 3; ++i) {
    const auto r = actionRect(i);
    if (mappedInput.wasTapInRect(r.x, r.y, r.width, r.height)) {
      activate(i);
      break;
    }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    selected_ = (selected_ + 2) % 3;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selected_ = (selected_ + 1) % 3;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activate(selected_);
  const auto value = HalCamera::snapshot();
  if (memcmp(&shown_, &value, sizeof(value))) {
    RenderLock lock;
    shown_ = value;
    if (!HalCamera::busy() && value.generation != generation_) {
      generation_ = value.generation;
      imageReady_ = decodeFrame();
      nextPreview_ = millis() + 3000;
      if (!imageReady_) paused_ = true;
    }
    if (value.state == HalCamera::State::Error) paused_ = true;
    requestUpdate();
  }
  if (diagnostic_ && !diagnosticSaved_ && generation_ >= 3 && imageReady_ && !HalCamera::busy()) {
    paused_ = true;
    requestUpdateAndWait();
    {
      RenderLock lock;
      ScreenshotUtil::saveFramebufferAsBmp("/.crosspoint/camera-view-test.bmp", renderer.getFrameBuffer(),
                                           renderer.getDisplayWidth(), renderer.getDisplayHeight());
    }
    diagnosticSaved_ = HalCamera::saveCurrent();
  }
  if (diagnostic_ && !HalCamera::busy() &&
      (shown_.state == HalCamera::State::Error || (diagnosticSaved_ && shown_.state == HalCamera::State::Saved))) {
    if (!diagnosticDoneAt_) diagnosticDoneAt_ = millis() + 1500;
    if (millis() >= diagnosticDoneAt_) {
      finish();
      return;
    }
  }
  if (saveQueued_ && !HalCamera::busy()) {
    saveQueued_ = false;
    requestUpdateAndWait();  // Paint the frozen frame before saving its JPEG.
    HalCamera::saveCurrent();
  } else if (!paused_ && !HalCamera::busy() && bitmap_ && decoder_ && millis() >= nextPreview_) {
    HalCamera::requestPreview();
    nextPreview_ = millis() + 3000;
  }
}
void MetalioCameraActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  const char* status = shown_.state == HalCamera::State::Error   ? tr(STR_CELL_ERROR)
                       : shown_.state == HalCamera::State::Saved ? tr(STR_CAMERA_SAVED)
                       : !imageReady_                            ? tr(STR_CAMERA_CONNECTING)
                       : paused_                                 ? tr(STR_CAMERA_PAUSED)
                                                                 : tr(STR_CAMERA_VIEWFINDER);
  GUI.drawHeader(renderer, Rect{0, m.topPadding, renderer.getScreenWidth(), m.headerHeight}, tr(STR_CAMERA_TITLE),
                 status);
  const auto area = viewRect();
  const int y = area.y + (area.height - height_) / 2;
  renderer.drawRect(area.x - 2, area.y - 2, area.width + 4, area.height + 4, true);
  if (imageReady_) {
    for (int row = 0; row < height_; ++row)
      for (int x = 0; x < width_; ++x)
        if (bitmap_[static_cast<size_t>(row) * (width_ / 8) + x / 8] & (0x80 >> (x & 7)))
          renderer.drawPixel(area.x + x, y + row, true);
  } else
    renderer.drawCenteredText(UI_10_FONT_ID, area.y + area.height / 2, status);
  renderer.drawCenteredText(UI_10_FONT_ID, area.y + area.height + 20, tr(STR_CAMERA_LIVE_HINT));
  const char* labels[] = {tr(STR_CAMERA_SHUTTER), paused_ ? tr(STR_CAMERA_RESUME) : tr(STR_CAMERA_PAUSE),
                          tr(STR_GALLERY_TITLE)};
  for (int i = 0; i < 3; ++i) {
    const auto r = actionRect(i);
    GUI.drawActionButton(renderer, r, labels[i]);
    if (i == selected_) renderer.drawRect(r.x - 2, r.y - 2, r.width + 4, r.height + 4, true);
  }
  const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
#endif
