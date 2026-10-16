#include "MetalioPlusDriver.h"
#include <PlusFramebuffer.h>
#include <metalio_epd.h>
#include <epd_board_metalio_eink4_plus.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <NativeGrayRefreshPolicy.h>
#include <cstring>

namespace freeink {
namespace {
uint8_t nextGrayProfile = 0;
ricky::gray::Area nextNavigation;
uint8_t contentEvery = 8, smallEvery = 32;
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
    nextGrayProfile = 0;
    ledger_ = {};
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
  bool restoreBaseline(uint8_t* proxy) {
    auto* hl = ready_ ? MetalioEpd_Hl() : nullptr;
    if (!hl || !hl->front_fb || !hl->back_fb || !proxy || state_ == OpticalState::Unknown) return false;
    memcpy(hl->front_fb, hl->back_fb, geometry().bufferSize * 4);
    restoreProxy(proxy);
    return true;
  }
  bool commitGrayscale16(const uint8_t* proxy) override {
    const uint8_t profile = nextGrayProfile;
    nextGrayProfile = 0;
    if (!ready_) return false;
    bool success;
    if (profile == 4 && state_ != OpticalState::Unknown) {
      success = refreshUi();
    } else {
      ledger_ = {};
      const bool full = profile != 1;
      success = refresh(MetalioEpd_Hl(), full ? MODE_GC16 : MODE_GL16, full) == EPD_DRAW_SUCCESS;
    }
    state_ = success ? OpticalState::Gray : OpticalState::Unknown;
    if (success && proxy) lastProxy_ = const_cast<uint8_t*>(proxy);  // Facade-owned stable BW surface.
    if (!success) ledger_ = {};
    return success;
  }
  void cancelGrayscale16() override {
    nextGrayProfile = 0;
    if (lastProxy_ && state_ != OpticalState::Unknown) restoreBaseline(lastProxy_);
  }
  void requestResync(uint8_t) override { state_ = OpticalState::Unknown; ledger_ = {}; nextGrayProfile = 0; }
 private:
  void restoreProxy(uint8_t* proxy) {
    const auto* gray = MetalioEpd_Hl()->back_fb;
    for (size_t i = 0; i < geometry().bufferSize; ++i) {
      uint8_t value = 0;
      for (unsigned pair = 0; pair < 4; ++pair) {
        const uint8_t pixels = gray[i * 4 + pair];
        if ((pixels & 15) >= 8) value |= 0x80u >> (pair * 2);
        if ((pixels >> 4) >= 8) value |= 0x40u >> (pair * 2);
      }
      proxy[i] = value;
    }
  }
  bool refreshUi() {
    auto* hl = MetalioEpd_Hl();
    if (!hl || !hl->front_fb || !hl->back_fb) return false;
    const auto nav = ricky::gray::navigationZone(nextNavigation, geometry().width, geometry().height);
    const auto damage = ricky::gray::difference(hl->front_fb, hl->back_fb, geometry().width, geometry().height, nav);
    auto candidate = ledger_;
    const auto plan = candidate.advance(damage, nav, contentEvery, smallEvery);
    if (!plan.count) return true;  // Identical native frame: no rails and no aging.
    const int64_t startedUs = esp_timer_get_time();
    epd_poweron();
    auto result = EPD_DRAW_EMPTY_LINE_QUEUE;
    if (epd_board_metalio_eink4_plus_tps_ok()) {
      for (uint8_t i = 0; i < plan.count; ++i) {
        const auto& step = plan.steps[i];
        EpdRect area = {step.area.x, step.area.y, step.area.width, step.area.height};
        result = step.cleanup ? epd_hl_update_area_full(hl, MODE_GC16, 25, area)
                              : epd_hl_update_area(hl, MODE_GL16, 25, area);
        if (result != EPD_DRAW_SUCCESS) break;
      }
    }
    epd_poweroff();
    ESP_LOGI("PlusDisplay", "regional steps=%u result=0x%x ms=%lld end=%lld", plan.count, result,
             (esp_timer_get_time() - startedUs) / 1000, esp_timer_get_time() / 1000);
    if (result != EPD_DRAW_SUCCESS) return false;
    ledger_ = candidate;
    return true;
  }
  EpdDrawError refresh(EpdiyHighlevelState* hl, EpdDrawMode waveform, bool full) {
    // A native gray frame and a full application/page change need a clean
    // optical baseline, not only a RAM diff against the previous UI image.
    const bool clean = state_ == OpticalState::Unknown;
    if (clean || full || (state_ == OpticalState::Gray && waveform == MODE_DU)) waveform = MODE_GC16;
    const int64_t startedUs = esp_timer_get_time();
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
    ESP_LOGI("PlusDisplay", "refresh mode=%d full=%d clean=%d previous=%d result=0x%x ms=%lld end=%lld",
             waveform, full, clean, static_cast<int>(state_), result, (esp_timer_get_time() - startedUs) / 1000,
             esp_timer_get_time() / 1000);
    return result;
  }
  ricky::gray::Ledger ledger_;
  uint8_t* lastProxy_ = nullptr;
  bool ready_ = false;
  OpticalState state_ = OpticalState::Unknown;
};
}
PanelDriver& metalioPlusDriver() {
  static MetalioPlusDriver driver;
  return driver;
}
void metalioPlusSetGray16Profile(uint8_t profile) { nextGrayProfile = profile; }
void metalioPlusSetGray16UiRefresh(int x, int y, int width, int height, uint8_t content, uint8_t small) {
  nextNavigation = {x, y, width, height};
  contentEvery = content ? content : 8;
  smallEvery = small ? small : 32;
  nextGrayProfile = 4;
}
bool metalioPlusRestoreGray16Baseline(uint8_t* proxy) {
  return static_cast<MetalioPlusDriver&>(metalioPlusDriver()).restoreBaseline(proxy);
}
}
