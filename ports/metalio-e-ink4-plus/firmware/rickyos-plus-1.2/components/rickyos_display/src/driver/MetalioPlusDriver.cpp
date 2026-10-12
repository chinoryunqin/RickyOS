#include "MetalioPlusDriver.h"
#include <PlusFramebuffer.h>
#include <metalio_epd.h>
#include <epd_board_metalio_eink4_plus.h>
#include <esp_log.h>

namespace freeink {
namespace {
uint8_t nextGrayProfile = 0;
// No additional framebuffer allocation: EPDiy owns its front/back buffers in
// PSRAM; FreeInk's original native 1-bit buffer remains the application surface.
class MetalioPlusDriver final : public PanelDriver {
 public:
  uint32_t spiHz() const override { return 0; }
  BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveHigh; }
  PanelGeometry geometry() const override { return {1216, 684, 152, 103968}; }
  bool usesExternalBus() const override { return true; }
  void begin(EpdBus&) override {
    ready_ = MetalioEpd_Init();
    state_ = OpticalState::Unknown;
    if (!ready_) ESP_LOGE("PlusDisplay", "panel initialization failed");
  }
  void deepSleep(EpdBus&) override {
    if (ready_) epd_poweroff();
  }
  OpticalState opticalState() const override { return state_; }
  void display(EpdBus&, const uint8_t* fb, const uint8_t*, RefreshMode mode, bool turnOff) override {
    if (!ready_ || !fb) return;
    auto* hl = MetalioEpd_Hl();
    if (!hl || !hl->front_fb) return;
    // FreeInk is native landscape, MSB-first 1bit, white=1. EPDiy stores
    // even pixels in the low nibble, white=15. Rotation belongs to GfxRenderer.
    plus::expandMonochrome(fb, geometry().bufferSize, hl->front_fb);
    const auto waveform = state_ == OpticalState::Unknown || mode == RefreshMode::Full ? MODE_GC16 : MODE_DU;
    const auto result = refresh(hl, waveform, mode == RefreshMode::Full);
    state_ = result == EPD_DRAW_SUCCESS ? OpticalState::Bw : OpticalState::Unknown;
    if (result != EPD_DRAW_SUCCESS) ESP_LOGE("PlusDisplay", "refresh failed: 0x%x", result);
    (void)turnOff;
  }
  uint8_t* beginGrayscale16() override {
    auto* hl = ready_ ? MetalioEpd_Hl() : nullptr;
    if (!hl || !hl->front_fb) return nullptr;
    plus::clearGrayscale(hl->front_fb, geometry().bufferSize * 4);
    return hl->front_fb;
  }
  bool commitGrayscale16(const uint8_t*) override {
    if (!ready_) return false;
    const bool full = nextGrayProfile != 1;
    nextGrayProfile = 0; // One-shot reader refresh-cycle decision.
    const auto result = refresh(MetalioEpd_Hl(), full ? MODE_GC16 : MODE_GL16, full);
    state_ = result == EPD_DRAW_SUCCESS ? OpticalState::Gray : OpticalState::Unknown;
    return result == EPD_DRAW_SUCCESS;
  }
  void requestResync(uint8_t) override { state_ = OpticalState::Unknown; }
 private:
  EpdDrawError refresh(EpdiyHighlevelState* hl, EpdDrawMode waveform, bool full) {
    // A native gray frame and a full application/page change need a clean
    // optical baseline, not only a RAM diff against the previous UI image.
    const bool clean = state_ == OpticalState::Unknown;
    if (clean || full || (state_ == OpticalState::Gray && waveform == MODE_DU)) waveform = MODE_GC16;
    epd_poweron();
    auto result = EPD_DRAW_EMPTY_LINE_QUEUE;
    if (epd_board_metalio_eink4_plus_tps_ok()) {
      if (clean) {
        // The physical panel can retain another firmware's image. Only reset
        // the back-buffer baseline after a successful physical white clear.
        result = epd_clear_checked();
        if (result == EPD_DRAW_SUCCESS)
          result = epd_hl_update_screen_from_white(hl, waveform, 25);
      } else if (full || waveform == MODE_GC16) {
        result = epd_hl_update_screen_full(hl, waveform, 25);
      } else {
        result = epd_hl_update_screen(hl, waveform, 25);
      }
    }
    epd_poweroff(); // TPS rails are only needed while the waveform runs.
    ESP_LOGI("PlusDisplay", "refresh mode=%d full=%d clean=%d previous=%d result=0x%x",
             waveform, full, clean, static_cast<int>(state_), result);
    return result;
  }
  bool ready_ = false;
  OpticalState state_ = OpticalState::Unknown;
};
}
PanelDriver& metalioPlusDriver() {
  static MetalioPlusDriver driver;
  return driver;
}
void metalioPlusSetGray16Profile(uint8_t profile) { nextGrayProfile = profile; }
}
