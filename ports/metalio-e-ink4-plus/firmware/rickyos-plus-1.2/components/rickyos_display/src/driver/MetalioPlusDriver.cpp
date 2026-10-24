#include "MetalioPlusDriver.h"
#include <PlusFramebuffer.h>
#if RICKYOS_PLUS_GRAY_CALIBRATION
#include <PlusGrayCalibration.h>
#endif
#include <metalio_epd.h>
#include <epd_board_metalio_eink4_plus.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <NativeGrayRefreshPolicy.h>
#include <cstdio>
#include <cstring>

namespace freeink {
namespace {
#if RICKYOS_PLUS_SCREEN_DUMP
// Diagnostic only: prints the panel's native 4-bit frame (1216x684, even pixel in the
// low nibble) after every successful update, for exact screenshots. Each line is
// "offset,bytes:" then (count, value) runs in hex, so a line a log message cut can be
// told apart and dropped; the frame goes out twice and the host merges the passes.
void dumpFrame(const uint8_t* fb, const char* kind) {
  static unsigned sequence = 0;
  constexpr size_t bytes = 1216 / 2 * 684;
  static const char hex[] = "0123456789abcdef";
  char line[24 + 64 * 4 + 2];
  const unsigned frame = sequence++;
  for (int pass = 0; pass < 2; ++pass) {
    printf("\n@@FRAME %u %s 1216 684\n", frame, kind);
    for (size_t i = 0; i < bytes;) {
      const size_t start = i;
      int used = snprintf(line, 24, "%x,", static_cast<unsigned>(start));
      const int countAt = used;
      used += 8;  // room for the byte count, filled in below
      line[used++] = ':';
      for (int token = 0; token < 64 && i < bytes; ++token) {
        const uint8_t value = fb[i];
        size_t run = 1;
        while (i + run < bytes && run < 255 && fb[i + run] == value) ++run;
        i += run;
        line[used++] = hex[run >> 4]; line[used++] = hex[run & 15];
        line[used++] = hex[value >> 4]; line[used++] = hex[value & 15];
      }
      char count[9];
      snprintf(count, sizeof(count), "%08x", static_cast<unsigned>(i - start));
      memcpy(line + countAt, count, 8);
      line[used++] = '\n'; line[used] = 0;
      fputs(line, stdout);
    }
    printf("@@END\n");
    fflush(stdout);
  }
}
#endif
uint8_t nextGrayProfile = 0;
ricky::gray::Area nextNavigation;
ricky::gray::Area nextReaderBody;
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
    overlayStage_ = 0;
    ready_ = MetalioEpd_Init();
    state_ = OpticalState::Unknown;
    if (!ready_) ESP_LOGE("PlusDisplay", "panel initialization failed");
  }
  void deepSleep(EpdBus&) override {
    discardOverlay();
    if (ready_) epd_poweroff();
  }
  OpticalState opticalState() const override { return state_; }
  void setBackgroundHint(bool darkBackground) override { darkBackground_ = darkBackground; }
  void display(EpdBus&, const uint8_t* fb, const uint8_t*, RefreshMode mode, bool turnOff) override {
#if RICKYOS_PLUS_GRAY_CALIBRATION
    calibrationHeld_ = false;  // Home/menu/BW navigation exits the diagnostic.
    calibrationShown_ = false;  // Returning to a book can open the card again.
#endif
    overlayStage_ = 0;
    nextGrayProfile = 0;
    nextReaderBody = {};
    ledger_ = {};
    if (!ready_ || !fb) return;
    auto* hl = MetalioEpd_Hl();
    if (!hl || !hl->front_fb) return;
    // FreeInk is native landscape, MSB-first 1bit, white=1. EPDiy stores
    // even pixels in the low nibble, white=15. Rotation belongs to GfxRenderer.
    plus::expandMonochrome(fb, geometry().bufferSize, hl->front_fb);
    // Light pages clean through the 16-gray path; Night mode is BW only, so its
    // HALF (the reader's due page, Refresh screen) must clean here, or it is DU.
    const bool full = mode == RefreshMode::Full || (darkBackground_ && mode == RefreshMode::Half);
    const auto waveform = state_ == OpticalState::Unknown || full ? MODE_GC16 : MODE_DU;
    const auto result = refresh(hl, waveform, full);
    state_ = result == EPD_DRAW_SUCCESS ? OpticalState::Bw : OpticalState::Unknown;
#if RICKYOS_PLUS_SCREEN_DUMP
    if (result == EPD_DRAW_SUCCESS) dumpFrame(hl->front_fb, "bw");
#endif
    if (result != EPD_DRAW_SUCCESS) ESP_LOGE("PlusDisplay", "refresh failed: 0x%x", result);
    (void)turnOff;
  }
  uint8_t* beginGrayscale16() override {
    overlayStage_ = 0;
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
    const auto readerBody = nextReaderBody;
    nextGrayProfile = 0;
    nextReaderBody = {};
    if (!ready_) return false;
#if RICKYOS_PLUS_GRAY_CALIBRATION
    // Generic retained redraws (profile 0), reading and cycle redraws must not
    // erase the samples before photographing. Only explicit UI navigation or
    // the B/W display path leaves the card; either route re-arms it.
    if (calibrationHeld_ && profile != 4) return true;
    if (profile == 4) calibrationShown_ = false;
    if (!calibrationShown_ && profile == 1) {
      calibrationShown_ = true;
      return showCalibration();
    }
    calibrationHeld_ = false;
#endif
    bool success;
    if (profile == 4 && state_ != OpticalState::Unknown) {
      success = refreshUi();
    } else {
      ledger_ = {};
      const bool full = profile != 1;
      // An ordinary reader turn drives the whole screen with the vendor GL16, as the vendor
      // firmware does. The held-diagonal table and the changed-rows mask left a stroke that
      // stays black across turns undriven until the next cleanup, so text faded between
      // cleanups (users saw faint ink; refreshing it only every 4th turn still read faint).
      // Its black-to-black cell erases and rewrites: body ink stays full. A reader
      // layout hint holds only unchanged chrome outside the text box in this same scan.
      if (!full && state_ != OpticalState::Unknown) {
        success = refreshTurn(MetalioEpd_Hl(), readerBody) == EPD_DRAW_SUCCESS;
      } else {
        success = refresh(MetalioEpd_Hl(), full ? MODE_GC16 : MODE_GL16_FAST, full) == EPD_DRAW_SUCCESS;
      }
    }
    state_ = success ? OpticalState::Gray : OpticalState::Unknown;
#if RICKYOS_PLUS_SCREEN_DUMP
    if (success) dumpFrame(MetalioEpd_Hl()->front_fb, "gray");
#endif
    if (success && proxy) lastProxy_ = const_cast<uint8_t*>(proxy);  // Facade-owned stable BW surface.
    if (!success) ledger_ = {};
    return success;
  }
  void cancelGrayscale16() override {
    overlayStage_ = 0;
    nextGrayProfile = 0;
    nextReaderBody = {};
    if (lastProxy_ && state_ != OpticalState::Unknown) restoreBaseline(lastProxy_);
  }
  void requestResync(uint8_t) override {
    overlayStage_ = 0;
    state_ = OpticalState::Unknown;
    ledger_ = {};
    nextGrayProfile = 0;
    nextReaderBody = {};
  }
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode mode = GrayscaleMode::Overlay) const override {
    if (mode != GrayscaleMode::Overlay) return {};
    return {GrayscaleEncoding::OverlayMasks, GrayscaleBase::Separate, false, false, false};
  }
  void copyGrayscaleLsb(EpdBus&, const uint8_t* lsb) override {
    discardOverlay();
    auto* hl = ready_ ? MetalioEpd_Hl() : nullptr;
    if (!lsb || !hl || !hl->front_fb || !hl->back_fb || state_ != OpticalState::Bw) return;
    plus::stageOverlayLsb(hl->back_fb, lsb, geometry().bufferSize, hl->front_fb);
    overlayStage_ = 1;
  }
  void copyGrayscaleMsb(EpdBus&, const uint8_t* msb) override {
    auto* hl = ready_ ? MetalioEpd_Hl() : nullptr;
    if (overlayStage_ != 1 || !msb || !hl || !hl->front_fb || !hl->back_fb || state_ != OpticalState::Bw) {
      discardOverlay();
      return;
    }
    plus::finishOverlayMsb(hl->back_fb, msb, geometry().bufferSize, hl->front_fb);
    overlayStage_ = 2;
  }
  void displayGray(EpdBus&, const uint8_t*, bool, const unsigned char* lut, bool factoryMode) override {
    // fb is the final MSB selector, not the page. Never use PanelDriver's
    // default display(fb): it turns untouched page margins into black ink.
    if (overlayStage_ != 2 || lut || state_ != OpticalState::Bw) {
      discardOverlay();
      return;
    }
    overlayStage_ = 0;
    nextGrayProfile = 0;
    nextReaderBody = {};
    ledger_ = {};
    const auto result = refresh(MetalioEpd_Hl(), factoryMode ? MODE_GC16 : MODE_GL16, factoryMode);
    state_ = result == EPD_DRAW_SUCCESS ? OpticalState::Gray : OpticalState::Unknown;
    if (result != EPD_DRAW_SUCCESS) ESP_LOGE("PlusDisplay", "overlay refresh failed: 0x%x", result);
  }
  void cleanupGrayscaleBuffers(EpdBus&, const uint8_t*) override { discardOverlay(); }
 private:
#if RICKYOS_PLUS_GRAY_CALIBRATION
  bool showCalibration() {
    auto* hl = MetalioEpd_Hl();
    if (!hl || !hl->front_fb || !hl->back_fb) return false;
    const plus::GrayCalibrationCard card{geometry().width, geometry().height};
    state_ = OpticalState::Unknown;  // Calibration always starts with a physical white clear.
    card.initial(hl->front_fb);
    auto result = refresh(hl, MODE_GC16, true);
    if (result == EPD_DRAW_SUCCESS) {
      // refresh() publishes the physically driven reference and white sample
      // interiors. Never invent a white baseline without physically drawing it.
      EpdRect cells[16];
      for (int i = 0; i < 16; ++i) {
        const int left = card.boundary(i), right = card.boundary(i + 1);
        cells[i] = {card.lower(), geometry().height - right + 1, card.bandHeight(), right - left - 3};
      }
      const int64_t started = esp_timer_get_time();
      epd_poweron();
      if (epd_board_metalio_eink4_plus_tps_ok()) {
        result = epd_calibrate_timed_samples(cells, 16);
      } else {
        result = EPD_DRAW_EMPTY_LINE_QUEUE;
      }
      epd_poweroff();
      ESP_LOGI("PlusDisplay", "CALIBRATION timed-drive 3..150ms then HOLD result=0x%x ms=%lld", result,
               (esp_timer_get_time() - started) / 1000);
    }
    // Sample codes are pulse indices, NOT measured optical gray values.
    // Force a physical clean when leaving this card; do not treat its targets
    // as a valid reading baseline or apply general grayscale deltas to it.
    state_ = OpticalState::Unknown;
    ledger_ = {};
    lastProxy_ = nullptr;
    calibrationHeld_ = result == EPD_DRAW_SUCCESS;
    return calibrationHeld_;
  }
  bool calibrationShown_ = false;
  bool calibrationHeld_ = false;
#endif
  void discardOverlay() {
    auto* hl = ready_ ? MetalioEpd_Hl() : nullptr;
    if (overlayStage_ && hl && hl->front_fb && hl->back_fb && state_ != OpticalState::Unknown)
      memcpy(hl->front_fb, hl->back_fb, geometry().bufferSize * 4);
    overlayStage_ = 0;
  }
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
        // As on the Read Pico, an ordinary UI step uses the text-turn table (MODE_GL16_INV
        // slot, see my_waveform.c): unchanged pixels are not driven and the old and new
        // marks cross-fade, so tab icons and labels do not flash and a moved marker fades
        // rather than being wiped. The ledger's cleanup steps still clean with GC16.
        result = step.cleanup ? epd_hl_update_area_full(hl, MODE_GC16, 25, area)
                              : epd_hl_update_area(hl, MODE_GL16_INV, 25, area);
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
  // Retained black in the body is erased and rewritten. With a layout hint,
  // equal chrome pixels are masked to no-drive without adding a second scan.
  EpdDrawError refreshTurn(EpdiyHighlevelState* hl, const ricky::gray::Area& body) {
    const int64_t startedUs = esp_timer_get_time();
    epd_poweron();
    auto result = EPD_DRAW_EMPTY_LINE_QUEUE;
    // The reader-turn table (MODE_GC16_FAST slot, my_waveform.c): the page lands at once
    // and black strokes that stay are briefly re-driven to keep their ink.
    if (epd_board_metalio_eink4_plus_tps_ok()) {
      if (body.width > 0 && body.height > 0) {
        result = epd_hl_update_screen_reader(hl, 25, {body.x, body.y, body.width, body.height});
      } else {
        result = epd_hl_update_screen_full(hl, MODE_GC16_FAST, 25);
      }
    }
    epd_poweroff();
    ESP_LOGI("PlusDisplay", "turn GL16 body=%d/%d/%d/%d result=0x%x ms=%lld", body.x, body.y,
             body.width, body.height, result, (esp_timer_get_time() - startedUs) / 1000);
    return result;
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
  uint8_t overlayStage_ = 0;  // 0: none, 1: LSB staged, 2: complete native target.
  bool ready_ = false;
  bool darkBackground_ = false;
  OpticalState state_ = OpticalState::Unknown;
};
}
PanelDriver& metalioPlusDriver() {
  static MetalioPlusDriver driver;
  return driver;
}
void metalioPlusSetGray16Profile(uint8_t profile) {
  nextGrayProfile = profile;
  nextReaderBody = {};
}
void metalioPlusSetGray16ReaderRefresh(int x, int y, int width, int height) {
  nextReaderBody = {x, y, width, height};
  nextGrayProfile = 1;
}
void metalioPlusSetGray16UiRefresh(int x, int y, int width, int height, uint8_t content, uint8_t small) {
  nextReaderBody = {};
  nextNavigation = {x, y, width, height};
  contentEvery = content ? content : 8;
  smallEvery = small ? small : 32;
  nextGrayProfile = 4;
}
bool metalioPlusRestoreGray16Baseline(uint8_t* proxy) {
  return static_cast<MetalioPlusDriver&>(metalioPlusDriver()).restoreBaseline(proxy);
}
}
