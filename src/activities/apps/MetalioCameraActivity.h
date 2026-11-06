#pragma once
#include <BoardConfig.h>
#if FREEINK_DEVICE_METALIO_EINK4
#include <HalCamera.h>
#include <HalMemory.h>
class JPEGDEC;
struct jpeg_draw_tag;

#include "activities/Activity.h"
class MetalioCameraActivity final : public Activity {
 public:
  MetalioCameraActivity(GfxRenderer& renderer, MappedInputManager& input, bool diagnostic = false);
  ~MetalioCameraActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool handleHomeGesture() override { return HalCamera::busy(); }
  void prepareForSleep() override { HalCamera::prepareForSleep(); }

 private:
  Rect viewRect() const;
  Rect actionRect(int index) const;
  static int decodedBlock(jpeg_draw_tag* block);
  bool decodeFrame();
  void activate(int index);
  HalMemory::PsramBuffer bitmap_;
  std::unique_ptr<JPEGDEC> decoder_;
  HalCamera::Snapshot shown_;
  int width_ = 0, height_ = 0, maxHeight_ = 0, sourceWidth_ = 0, sourceHeight_ = 0, selected_ = 0;
  uint32_t generation_ = 0, displayedGeneration_ = 0, nextPreview_ = 0;
  bool diagnostic_ = false, diagnosticSaved_ = false;
  uint32_t diagnosticDoneAt_ = 0;
  bool imageReady_ = false, paused_ = false, waitRelease_ = true;
};
#endif
