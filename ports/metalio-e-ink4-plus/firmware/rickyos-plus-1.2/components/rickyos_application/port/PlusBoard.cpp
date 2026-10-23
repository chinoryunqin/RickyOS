#include "PlusBoard.h"
#include "PlusAudio.h"
#include "PlusSleepBreadcrumb.h"
#if defined(RICKYOS_PLUS_STATIC_STANDBY) && defined(RICKYOS_STANDBY_DIAG)
#include <esp_attr.h>
#include <esp_system.h>
#endif
#include <InputManager.h>
#include <PowerManager.h>
#include <plus_i2c_wire.h>
#include <metalio_i2c1.h>
#include <epd_board_metalio_eink4_plus.h>
#include <config.h>
#include <esp_log.h>
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/usb_serial_jtag.h>
#include <hal/usb_serial_jtag_ll.h>
#include <esp_sleep.h>
#include <metalio_touch.h>
#include <StandbyDiag.h>
#include <lcd_fault.h>
#if defined(RICKYOS_PLUS_STATIC_STANDBY)
#include <metalio_sd.h>
#endif
#if defined(RICKYOS_PLUS_IDLE_FREQ_MHZ) && defined(RICKYOS_STANDBY_DIAG)
#include <BoardConfig.h>
#include <CrossPointSettings.h>
#endif
namespace plus_board {
namespace {
using SleepStage = plus_sleep_trace::Stage;
#if defined(RICKYOS_PLUS_STATIC_STANDBY) && defined(RICKYOS_STANDBY_DIAG)
RTC_NOINIT_ATTR volatile plus_sleep_trace::Record sleepTrace[2];
void sleepCheckpoint(SleepStage stage) { plus_sleep_trace::checkpoint(sleepTrace, stage, millis()); }
void recordSleepWake(unsigned cause, int error) {
  plus_sleep_trace::checkpoint(sleepTrace, SleepStage::SleepReturned, millis(), cause,
                               static_cast<uint32_t>(error), cause == ESP_SLEEP_WAKEUP_TIMER);
}
void reportSleepReset() {
  const auto saved = plus_sleep_trace::latest(sleepTrace);
  const auto& w = saved.record.words;
  const uint32_t last = saved.valid && w[5] ? w[6 + w[5] - 1] : 0;
  STANDBY_DIAG("reset reason=%u trace=%u last=%lu seq=%lu cycles=%lu t=%lu", unsigned(esp_reset_reason()),
               saved.valid, (unsigned long)last, (unsigned long)w[2], (unsigned long)w[3], (unsigned long)w[4]);
  if (saved.valid) STANDBY_DIAG("reset trail=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu", (unsigned long)w[6],
      (unsigned long)w[7], (unsigned long)w[8], (unsigned long)w[9], (unsigned long)w[10],
      (unsigned long)w[11], (unsigned long)w[12], (unsigned long)w[13]);
  if (saved.valid) STANDBY_DIAG("reset wake cause=%lu err=%ld timers=%lu", (unsigned long)w[14],
                              (long)static_cast<int32_t>(w[15]), (unsigned long)w[16]);
  sleepCheckpoint(SleepStage::Boot);
}
#else
void sleepCheckpoint(SleepStage) {}
void recordSleepWake(unsigned, int) {}
void reportSleepReset() {}
#endif
// Both physical candidates blanked the panel after sleep. Ordinary builds must
// remain awake until display recovery is proven on hardware; diagnostics alone
// must never opt a device into this experimental path.
#if defined(RICKYOS_PLUS_STANDBY_SLEEP) && RICKYOS_PLUS_STANDBY_SLEEP == 1
constexpr bool kStandbySleepEnabled = true;
#else
constexpr bool kStandbySleepEnabled = false;
#endif
bool boardReady = false;
bool sleepFault = false;
constexpr uint8_t kPowerKeyMask = 0x80;
constexpr uint8_t kAllKeyMask = 0x83;

bool readInputs(uint8_t& p0, uint8_t& p1) {
  // Each board accessor owns the non-recursive shared bus mutex internally.
  return epd_board_metalio_eink4_plus_ioexp_read_port(0, &p0) &&
         epd_board_metalio_eink4_plus_ioexp_read_port(1, &p1);
}

bool standbyPowerIdle() {
  // Adapter classification can be zero while a PC is attached (observed on
  // hardware). SOF detection independently protects the native USB connection.
  // Neither a zero classification nor a zero charge state proves cable absence;
  // a power-only adapter may still supply the board after charge termination.
  if (usb_serial_jtag_is_connected()) return false;
  uint8_t status = 0;
  if (!metalio_i2c1_lock(20)) return false;
  const auto error = plus_i2c_read(0x6b, 0x1e, &status, 1);
  metalio_i2c1_unlock();
  return error == ESP_OK && (status & 0x1f) == 0 && !usb_serial_jtag_is_connected();
}

void restoreUsbDebugAfterSleep() {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
  // OEM ReviveUsjAfterSdRestore: restore USJ clocks/pads and force a bounded
  // disconnect so a host can enumerate again. Static standby remounts SD before
  // this step; the USB-MSC routing switch remains unchanged.
  usb_serial_jtag_ll_enable_bus_clock(true);
  usb_serial_jtag_ll_phy_enable_pad(false);
  delay(150);
  usb_serial_jtag_ll_phy_set_defaults();
  usb_serial_jtag_ll_phy_enable_pad(true);
  // No wait for SOF: an unplugged device is a valid state, not a restore error.
#endif
}

bool touchIdle() {
  // Plus polls the FT6336U point frame, not its IRQ level. GPIO5 can stay low
  // without a contact. Accept only a successful zero-contact frame; malformed
  // frames, held cover keys and bus errors remain blockers. Stack storage only.
  uint8_t frame[5]{};
  if (!metalio_i2c1_lock(20)) return false;
  const auto error = plus_i2c_read(TOUCH_I2C_ADDR, 0x02, frame, sizeof(frame));
  metalio_i2c1_unlock();
  return error == ESP_OK && (frame[0] & 0x0f) == 0;
}

#if defined(RICKYOS_PLUS_STATIC_STANDBY)
// OEM sc7a20h.cc WriteSilenceRegs: low-active open-drain, all IRQ routes
// disabled, 1 Hz with axes off. Save/restore the polling configuration on stack.
struct StandbyImu {
  static constexpr uint8_t regs[] = {0x20, 0x22, 0x24, 0x25, 0x30};
  static constexpr uint8_t silence[] = {0x10, 0x00, 0x00, 0x03, 0x00};
  uint8_t saved[5]{};
  uint8_t address = 0;
  bool touched = false;
  bool park() {
    if (!metalio_i2c1_lock(20)) return false;
    for (uint8_t candidate : {uint8_t(SC7A20H_I2C_ADDR_DEFAULT), uint8_t(SC7A20H_I2C_ADDR_ALT)}) {
      uint8_t who = 0;
      if (plus_i2c_read(candidate, 0x0f, &who, 1) == ESP_OK && who == 0x11) { address = candidate; break; }
    }
    bool ok = address != 0;
    for (unsigned i = 0; ok && i < 5; ++i) ok = plus_i2c_read(address, regs[i], &saved[i], 1) == ESP_OK;
    if (ok) {
      touched = true;
      for (unsigned i : {4u, 1u, 2u, 0u, 3u}) {
        if (plus_i2c_write(address, regs[i], &silence[i], 1) != ESP_OK) ok = false;
      }
      uint8_t source = 0;
      if (plus_i2c_read(address, 0x31, &source, 1) != ESP_OK) ok = false;
      for (unsigned i = 0; i < 5; ++i) {
        uint8_t actual = 0xff;
        if (plus_i2c_read(address, regs[i], &actual, 1) != ESP_OK || actual != silence[i]) ok = false;
      }
    }
    metalio_i2c1_unlock();
    return ok;
  }
  bool restore() {
    if (!touched) return true;
    if (!metalio_i2c1_lock(20)) return false;
    bool ok = true;
    for (unsigned i : {4u, 1u, 2u, 3u, 0u}) {
      if (plus_i2c_write(address, regs[i], &saved[i], 1) != ESP_OK) ok = false;
    }
    for (unsigned i = 0; i < 5; ++i) {
      uint8_t actual = 0xff;
      if (plus_i2c_read(address, regs[i], &actual, 1) != ESP_OK || actual != saved[i]) ok = false;
    }
    metalio_i2c1_unlock();
    return ok;
  }
};
#endif

bool inputsIdle(uint8_t p1, bool rtcInput = false, bool checkIrq = true) {
  const int irqLevel = rtcInput ? rtc_gpio_get_level(IO_EXPANDER_INT_GPIO) : gpio_get_level(IO_EXPANDER_INT_GPIO);
  return (p1 & kAllKeyMask) == kAllKeyMask && (!checkIrq || irqLevel != 0) &&
         gpio_get_level(BOOT_BUTTON_GPIO) != BOOT_BUTTON_ACTIVE_LEVEL && touchIdle();
}

StandbyWake observePowerRelease() {
  // A confirmed low P17 after wake is a real press. Give short presses time to
  // finish; still-held presses belong to main.cpp's ordinary long-press owner.
  const uint32_t started = millis();
  uint32_t releasedAt = 0;
  bool releasing = false;
  bool down = true;
  while (uint32_t(millis() - started) < 250) {
    delay(10);
    uint8_t p0 = 0, p1 = 0;
    if (!readInputs(p0, p1)) return StandbyWake::Failed;
    if ((p1 & 3) != 3) return StandbyWake::Failed;
    down = (p1 & kPowerKeyMask) == 0;
    if (down) {
      releasing = false;
    } else if (!releasing) {
      releasedAt = millis();
      releasing = true;
    } else if (uint32_t(millis() - releasedAt) >= 40) {
      return StandbyWake::PowerButton;
    }
  }
  return down ? StandbyWake::PowerButtonHeld : StandbyWake::Failed;
}

uint8_t buttons() {
  uint8_t value = 0xff;
  if (!metalio_i2c1_lock(20)) return 0;
  const auto result = plus_i2c_read(0x20, 0x01, &value, 1);
  metalio_i2c1_unlock();
  if (result != ESP_OK) return 0;
  uint8_t keys = 0;
  if (!(value & 0x80)) keys |= 1u << InputManager::BTN_POWER;
  if (!(value & 0x01)) keys |= 1u << InputManager::BTN_UP;
  if (!(value & 0x02)) keys |= 1u << InputManager::BTN_DOWN;
  return keys;
}
void shutdown() {
  plus_audio::shutdown();
  // Match the OEM PwrOffTask: a returning pulse train means the CPU is still
  // powered, not that shutdown succeeded. Keep requesting hardware power-off
  // instead of returning to the SDK's indefinite idle fallback. The board
  // routine preserves the OEM pulse waveform; no charger/BATFET writes here.
  ESP_LOGW("PlusBoard", "requesting hardware power-off until the rail is cut");
  for (;;) {
    epd_board_metalio_eink4_plus_pwr_key_pulse_train();
    delay(200);
  }
}
}
bool begin() {
  reportSleepReset();
  if (plus_i2c_begin() != ESP_OK) return false;
  if (!epd_board_metalio_eink4_plus_safe_io_init()) return false;
  epd_board_metalio_eink4_plus_tp_reset();
  InputManager::setButtonHook(buttons);
  freeink::PowerManager::setHostShutdownHook(shutdown);
  boardReady = true;
  return true;
}

bool idleCpuFrequencyAllowed() { return !plus_audio::busy(); }

void traceIdlePower() {
#if defined(RICKYOS_PLUS_IDLE_FREQ_MHZ) && defined(RICKYOS_STANDBY_DIAG)
  STANDBY_DIAG("idle cfg: face=%u off=%u off_s=%lu sleep=%u", SETTINGS.rickyStandbyFace,
               SETTINGS.rickyPowerOffScreen, SETTINGS.getStandbyPowerOffMs() / 1000UL, kStandbySleepEnabled);
  const uint8_t addr = BoardConfig::ACTIVE.batteryGauge.gaugeAddr;
  if (addr == 0 || !metalio_i2c1_lock(20)) {
    STANDBY_DIAG("idlepower: cpu=%u read_error=1", getCpuFrequencyMhz());
    return;
  }
  // Same read-only BQ registers as BatteryMonitor. Four stack words only;
  // neither the gauge nor the charger is configured by this diagnostic.
  constexpr uint8_t regs[] = {0x08, 0x0c, 0x14, 0x22};
  uint16_t values[4]{};
  bool ok = true;
  for (unsigned i = 0; i < 4; ++i) {
    uint8_t raw[2]{};
    if (plus_i2c_read(addr, regs[i], raw, sizeof(raw)) != ESP_OK) {
      ok = false;
      break;
    }
    values[i] = uint16_t(raw[0]) | (uint16_t(raw[1]) << 8);
  }
  metalio_i2c1_unlock();
  if (!ok) {
    STANDBY_DIAG("idlepower: cpu=%u read_error=1", getCpuFrequencyMhz());
    return;
  }
  STANDBY_DIAG("idlepower: cpu=%u mv=%u i=%d avg=%d q=%u", getCpuFrequencyMhz(), values[0],
               static_cast<int16_t>(values[1]), static_cast<int16_t>(values[2]), values[3]);
#endif
}

bool standbySleepReady() {
  // A restored allocation can succeed while the first real frame underruns.
  // Preserve that first fault and remain awake instead of repeating the same
  // failed sleep/refresh cycle (observed at row 426 after the first timer wake).
  if (kStandbySleepEnabled && !sleepFault && epd_lcd_get_first_fault() != 0) {
    sleepFault = true;
    ESP_LOGE("PlusSleep", "display first fault=0x%08lx; sleep disabled until restart",
             static_cast<unsigned long>(epd_lcd_get_first_fault()));
  }
#ifdef RICKYOS_STANDBY_DIAG
  static uint32_t lastReadyNote = 0;
  if (uint32_t(millis() - lastReadyNote) >= 30000) {
    lastReadyNote = millis();
    STANDBY_DIAG("board ready=%u fault=%u audio=%u touch_pending=%u", boardReady, sleepFault,
                 plus_audio::busy(), metalio_touch_has_pending_edges());
  }
#endif
  return kStandbySleepEnabled && boardReady && !sleepFault && !plus_audio::busy() && !metalio_touch_has_pending_edges();
}

static StandbyWake standbySleepOnce(const uint32_t seconds, StandbyStorage* storage) {
  const bool staticPark = storage != nullptr;
  const auto skip = [](const char* reason) {
#ifdef RICKYOS_STANDBY_DIAG
    static const char* previousReason = nullptr;
    static uint32_t lastNote = 0;
    if (reason != previousReason || uint32_t(millis() - lastNote) >= 30000) {
      previousReason = reason;
      lastNote = millis();
      STANDBY_DIAG("board skip: %s", reason);
    }
#else
    (void)reason;
#endif
    return StandbyWake::Failed;
  };
  if (!standbySleepReady()) return skip("not ready/audio/touch pending");
  if (seconds == 0 || seconds > 60) return skip("timer range");
  if (!standbyPowerIdle()) return skip("USB/charger active or read failed");
  uint8_t p0 = 0, p1 = 0;
  if (!readInputs(p0, p1)) return skip("expander read failed");
  if (!inputsIdle(p1, false, !staticPark)) {
    STANDBY_DIAG("inputs p1=%02x irq=%d touch=%d boot=%d", p1, gpio_get_level(IO_EXPANDER_INT_GPIO),
                 gpio_get_level(TOUCH_INT_PIN), gpio_get_level(BOOT_BUTTON_GPIO));
    return skip("input active");
  }
  gpio_io_config_t config{};
  if (gpio_get_io_config(IO_EXPANDER_INT_GPIO, &config) != ESP_OK || !config.ie || config.oe)
    return skip("GPIO2 not input");
  if (!esp_sleep_is_valid_wakeup_gpio(IO_EXPANDER_INT_GPIO)) return skip("GPIO2 not EXT1 capable");

  // Match the OEM's low-level EXT1 wake without changing the normal digital
  // ISR type. Take ownership of the RTC input explicitly: IDF's EXT1 prepare
  // selects the RTC mux and may hold the pad, but disabling EXT1 does not undo
  // either. Keep the CPU ISR masked until the pad is digital again.
  if (gpio_intr_disable(IO_EXPANDER_INT_GPIO) != ESP_OK) return StandbyWake::Failed;
  bool rtcTouched = false;
  bool sourceTouched = false;
  bool timerTouched = false;
  const auto cleanup = [&] {
    esp_err_t firstError = ESP_OK;
    const auto keepError = [&](esp_err_t error) {
      if (firstError == ESP_OK && error != ESP_OK) firstError = error;
    };
    // Remove only our pin, preserving unrelated EXT1 configuration. Release
    // the sleep hold before rtc_gpio_deinit returns the pin to digital control
    // and clears the RTC input/pulls/clock ownership acquired by rtc_gpio_init.
    if (sourceTouched) keepError(esp_sleep_disable_ext1_wakeup_io(1ULL << IO_EXPANDER_INT_GPIO));
    if (timerTouched) keepError(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER));
    if (rtcTouched) {
      keepError(rtc_gpio_hold_dis(IO_EXPANDER_INT_GPIO));
      keepError(rtc_gpio_deinit(IO_EXPANDER_INT_GPIO));
    }
    keepError(gpio_intr_enable(IO_EXPANDER_INT_GPIO));
    if (firstError != ESP_OK) {
      sleepFault = true;
      ESP_LOGE("PlusSleep", "wake cleanup failed: %s; sleep disabled until restart", esp_err_to_name(firstError));
    }
    return firstError == ESP_OK;
  };

  rtcTouched = true;
  esp_err_t error = rtc_gpio_init(IO_EXPANDER_INT_GPIO);
  if (error == ESP_OK) error = rtc_gpio_set_direction(IO_EXPANDER_INT_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
  // S31 has separate digital and RTC pull registers. Mirror the existing
  // electrical configuration, without changing the digital sleep selection.
  if (error == ESP_OK)
    error = config.pu ? rtc_gpio_pullup_en(IO_EXPANDER_INT_GPIO) : rtc_gpio_pullup_dis(IO_EXPANDER_INT_GPIO);
  if (error == ESP_OK)
    error = config.pd ? rtc_gpio_pulldown_en(IO_EXPANDER_INT_GPIO) : rtc_gpio_pulldown_dis(IO_EXPANDER_INT_GPIO);
  if (error == ESP_OK) {
    sourceTouched = true;
    error = esp_sleep_enable_ext1_wakeup_io(1ULL << IO_EXPANDER_INT_GPIO, ESP_EXT1_WAKEUP_ANY_LOW);
  }
  if (error == ESP_OK) {
    timerTouched = true;
    error = esp_sleep_enable_timer_wakeup(uint64_t(seconds) * 1000000ULL);
  }
  if (error != ESP_OK) {
    cleanup();
    ESP_LOGW("PlusSleep", "wake setup failed: %s", esp_err_to_name(error));
    return StandbyWake::Failed;
  }

  // Recheck after arming to close the read/configure race. Never hold the shared
  // I2C mutex across sleep, change expander directions, or cut peripheral rails.
  if (!standbyPowerIdle() || !readInputs(p0, p1)) {
    cleanup();
    return StandbyWake::Failed;
  }
  if ((p1 & kPowerKeyMask) == 0) {
    if (!cleanup()) return StandbyWake::Failed;
    if ((p1 & 3) != 3) return StandbyWake::Failed;
    return observePowerRelease();
  }
  if (!inputsIdle(p1, true, !staticPark)) {
    cleanup();
    return StandbyWake::Failed;
  }

#if defined(RICKYOS_PLUS_STATIC_STANDBY)
  StandbyImu imu;
  bool storageStopped = false, sdPadsTouched = false, keysTouched = false, railsTouched = false;
  const auto restorePeripherals = [&] {
    bool ok = true;
    bool keysRestored = true;
    if (keysTouched) {
      sleepCheckpoint(SleepStage::KeysRestore);
      keysRestored = epd_board_metalio_eink4_plus_ioexp_restore_keys_checked();
    }
    bool railsRestored = true;
    if (railsTouched) {
      sleepCheckpoint(SleepStage::RailsOn);
      railsRestored = epd_board_metalio_eink4_plus_sleep_restore_rails_staged(keysRestored, [](unsigned phase) {
        if (phase < 6) sleepCheckpoint(static_cast<SleepStage>(static_cast<uint32_t>(SleepStage::RailP14Write) + phase));
      });
    }
    const bool ioRestored = keysRestored && (!keysTouched || epd_board_metalio_eink4_plus_sleep_io_checked(false));
    sleepCheckpoint(SleepStage::SdRelease);
    const bool padsRestored = !sdPadsTouched || MetalioSd_ResumeBusChecked();
    if (!railsRestored || !ioRestored || !padsRestored) ok = false;
    // Remount through the SAME SDK manager, then restore every readonly position
    // before USJ revives. The legacy VFS MetalioSd_Init is deliberately not used.
    sleepCheckpoint(SleepStage::SdMount);
    const bool sdRestored = !storageStopped || storage->resume(storage->context);
    sleepCheckpoint(SleepStage::ImuRestore);
    const bool imuRestored = imu.restore();
    if (!sdRestored || !imuRestored) ok = false;
    if (!ok) sleepFault = true;
    STANDBY_DIAG("static restore: keys=%u rails=%u io=%u pads=%u sd=%u imu=%u ok=%u", keysRestored, railsRestored, ioRestored, padsRestored,
                 sdRestored, imuRestored, ok);
    return ok;
  };
  if (staticPark) {
    sleepCheckpoint(SleepStage::SdStop);
    if (!storage->suspend(storage->context)) { cleanup(); return skip("storage busy/unsupported"); }
    storageStopped = true;
    sdPadsTouched = true;
    sleepCheckpoint(SleepStage::SdPads);
    const bool sdParked = MetalioSd_ParkBusChecked();
    sleepCheckpoint(SleepStage::TpsSleep);
    epd_board_metalio_eink4_plus_tps_enter_sleep();
    // Bus resources are released below, once, before the key/rail handoff.
    sleepCheckpoint(SleepStage::BusPark);
    epd_board_metalio_eink4_plus_bus_hold_low();
    sleepCheckpoint(SleepStage::ImuPark);
    const bool imuParked = imu.park();
    keysTouched = true;
    sleepCheckpoint(SleepStage::KeysMask);
    epd_board_metalio_eink4_plus_ioexp_mask_for_sleep();
    sleepCheckpoint(SleepStage::PowerInput);
    const bool powerArmed = epd_board_metalio_eink4_plus_ioexp_enable_power_wake_pin();
    uint8_t check0 = 0, check1 = 0;
    const bool inputsRead = readInputs(check0, check1);
    const int irqLevel = rtc_gpio_get_level(IO_EXPANDER_INT_GPIO);
    bool parked = sdParked && imuParked && powerArmed && inputsRead &&
                  (check1 & kPowerKeyMask) != 0 && irqLevel != 0;
    int ioChecked = -1;
    if (parked) {
      railsTouched = true;
      sleepCheckpoint(SleepStage::RailsOff);
      epd_board_metalio_eink4_plus_sleep_cut_rails();
      sleepCheckpoint(SleepStage::IoCheck);
      parked = epd_board_metalio_eink4_plus_sleep_io_checked(true);
      ioChecked = parked;
    }
    STANDBY_DIAG("static park: sd=%u imu=%u key=%u input=%u p1=%02x irq=%d io=%d", sdParked,
                 imuParked, powerArmed, inputsRead, check1, irqLevel, ioChecked);
    if (!parked) {
      cleanup();
      const bool peripheralOk = restorePeripherals();
      sleepCheckpoint(SleepStage::UsbRestore);
      restoreUsbDebugAfterSleep();
      sleepCheckpoint(SleepStage::LcdRestore);
      const bool displayOk = epd_board_metalio_eink4_plus_bus_restore_checked();
      if (!peripheralOk || !displayOk) sleepFault = true;
      return skip("static park failed; restored");
    }
  }
#else
  if (staticPark) { cleanup(); return StandbyWake::Failed; }
#endif

  // The custom LCD/GDMA/RMT path does not register IDF light-sleep retention.
  // Use the board's existing shallow-sleep lifecycle under the caller's render
  // lock. Deinit frees its existing DMA resources before restore reallocates
  // them; EPDiy's image/line queues remain owned by the display backend.
  // Static profile also parks SD/TPS/keys/rails above; no charger writes.
  if (!staticPark) epd_board_metalio_eink4_plus_bus_hold_low();
  sleepCheckpoint(SleepStage::BusLatch);
  epd_board_metalio_eink4_plus_bus_latch_for_sleep();
  ESP_LOGI("PlusSleep", "enter: seconds=%lu ext1_gpio=2", static_cast<unsigned long>(seconds));
  STANDBY_DIAG("board sleep enter: seconds=%lu display_fault=0x%08lx", static_cast<unsigned long>(seconds),
               static_cast<unsigned long>(epd_lcd_get_first_fault()));
  sleepCheckpoint(SleepStage::SleepEnter);
  error = esp_light_sleep_start();
  const auto cause = esp_sleep_get_wakeup_cause();
  recordSleepWake(static_cast<unsigned>(cause), error);
  // As in the OEM wake sequence, latch P17 before restoring peripherals or
  // re-enabling the shared GPIO ISR: LCD reinitialization may outlast a tap.
  // Always restore the bus after a failed read; never turn that read into a key.
  sleepCheckpoint(SleepStage::WakeLatch);
  const bool wakeInputsValid = error == ESP_OK && readInputs(p0, p1);
  const uint8_t wakeP1 = p1;
  STANDBY_DIAG("wake latch: valid=%u p1=%02x cause=%d", wakeInputsValid, wakeP1, static_cast<int>(cause));
  sleepCheckpoint(SleepStage::WakeCleanup);
  const bool restored = cleanup();
#if defined(RICKYOS_PLUS_STATIC_STANDBY)
  const bool peripheralRestored = !staticPark || restorePeripherals();
#else
  const bool peripheralRestored = true;
#endif
  // Restore even on a rejected/failed sleep or GPIO cleanup error, before any
  // Activity can redraw or process the key. Never leave the bus held low.
  // Keep debug recovery independent of LCD allocation, including a rejected
  // sleep attempt: clock/pad preparation may already have run on that path.
  // An unplugged device is allowed; restoration never waits for a host.
  sleepCheckpoint(SleepStage::UsbRestore);
  restoreUsbDebugAfterSleep();
  STANDBY_DIAG("USB restore done; display restore begin");
  sleepCheckpoint(SleepStage::LcdRestore);
  const bool displayRestored = epd_board_metalio_eink4_plus_bus_restore_checked();
  STANDBY_DIAG("display restore done: ok=%u", displayRestored);
  if (!displayRestored) {
    sleepFault = true;
    ESP_LOGE("PlusSleep", "LCD restore failed; sleep disabled until restart");
  }
  STANDBY_DIAG("board sleep return: error=%d cause=%d cleanup=%u", static_cast<int>(error),
               static_cast<int>(cause), restored);
  if (error != ESP_OK || !restored || !displayRestored || !peripheralRestored) {
    if (error != ESP_OK) ESP_LOGW("PlusSleep", "sleep failed: %s", esp_err_to_name(error));
    return StandbyWake::Failed;
  }
  if (!wakeInputsValid) return StandbyWake::Failed;
  ESP_LOGI("PlusSleep", "wake: cause=%d p0=0x%02x p1=0x%02x irq=%d", static_cast<int>(cause), p0, wakeP1,
           gpio_get_level(IO_EXPANDER_INT_GPIO));
  if ((wakeP1 & kPowerKeyMask) == 0) {
    if ((wakeP1 & 3) != 3) return StandbyWake::Failed;
    return observePowerRelease();
  }
  // GPIO2 also carries RTC/IMU/TPS events. A pulse already released before the
  // first successful read cannot be attributed to P17; never invent a key press.
  return cause == ESP_SLEEP_WAKEUP_TIMER ? StandbyWake::Timer : StandbyWake::Failed;
}
StandbyWake standbyLightSleep(const uint32_t seconds) { return standbySleepOnce(seconds, nullptr); }
StandbyWake standbyStaticSleep(const uint32_t seconds, StandbyStorage& storage) {
  const auto reason = standbySleepOnce(seconds, &storage);
  switch (reason) {
    case StandbyWake::Timer: sleepCheckpoint(SleepStage::ExitTimer); break;
    case StandbyWake::PowerButton: sleepCheckpoint(SleepStage::ExitPower); break;
    case StandbyWake::PowerButtonHeld: sleepCheckpoint(SleepStage::ExitHeld); break;
    default: sleepCheckpoint(SleepStage::ExitFailed); break;
  }
  return reason;
}
}
