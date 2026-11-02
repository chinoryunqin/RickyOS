#pragma once

#include <cstdint>

namespace plus_board {

// Diagnostic RAM only. All fields have fixed widths for offline JTAG decoding.
// The gauge integrates charge while the CPU sleeps; this class never schedules a wake.
enum class PowerCaptureMode : uint32_t { Awake, Standby };
enum class PowerWindowState : uint32_t { Empty, Recording, Frozen };
enum PowerCaptureFault : uint32_t {
  PowerReadFailed = 1u,
  PowerCharging = 2u,
  PowerCounterReset = 4u,
  PowerCounterJump = 8u,
  PowerExternalDuringStandby = 16u,
};

struct PowerCaptureSample {
  uint32_t atMs = 0;
  uint16_t counter = 0;
  uint16_t mv = 0;
  int16_t current = 0;
  int16_t average = 0;
  uint16_t operation = 0;
  uint16_t valid = 0;
};

struct PowerCaptureWindow {
  PowerCaptureSample first{};
  PowerCaptureSample last{};
  uint32_t samples = 0;
  uint32_t faults = 0;
  PowerWindowState state = PowerWindowState::Empty;

  void observe(const PowerCaptureSample& sample) {
    if (state == PowerWindowState::Frozen) return;
    if (!sample.valid) {
      faults |= PowerReadFailed;
      return;
    }
    // A filtered average can remain positive after unplugging. Do not mistake
    // it for ongoing charging. Skip a stale charging-direction first sample;
    // subsequent positive instantaneous current invalidates the active window.
    if (sample.current > 5) {
      if (samples != 0) faults |= PowerCharging;
      return;
    }
    if (samples == 0) {
      first = sample;
    } else {
      const uint16_t rawDelta = uint16_t(sample.counter - last.counter);
      const int32_t delta = rawDelta <= 32767 ? int32_t(rawDelta) : int32_t(rawDelta) - 65536;
      const uint32_t elapsed = uint32_t(sample.atMs - last.atMs);
      // 2 A is a corruption guard, not a charger setting. Two mAh tolerate
      // endpoint quantization. Do not silently convert gauge resets to discharge.
      const uint64_t maximum = uint64_t(elapsed) * 2000u / 3600000u + 2u;
      if (delta < 0 || (sample.counter == 0 && last.counter > 2 && last.counter < 65534))
        faults |= PowerCounterReset;
      if (delta > 0 && uint32_t(delta) > maximum) faults |= PowerCounterJump;
    }
    last = sample;
    ++samples;
    state = PowerWindowState::Recording;
  }

  void freeze() {
    if (state == PowerWindowState::Recording) state = PowerWindowState::Frozen;
  }
};

struct PowerCapture {
  uint32_t magic = 0x504f5752;
  uint32_t version = 1;
  uint32_t cycle = 0;
  PowerCaptureMode mode = PowerCaptureMode::Awake;
  uint32_t rearmOnBattery = 1;
  PowerCaptureWindow awake{};
  PowerCaptureWindow standby{};

  void observe(const PowerCaptureSample& sample, bool batteryOnly) {
    if (!batteryOnly) {
      if (mode == PowerCaptureMode::Standby && standby.state == PowerWindowState::Recording)
        standby.faults |= PowerExternalDuringStandby;
      activeWindow().freeze();
      rearmOnBattery = 1;
      return;
    }
    if (rearmOnBattery) {
      awake = {};
      standby = {};
      ++cycle;
      rearmOnBattery = 0;
    }
    activeWindow().observe(sample);
  }

  void enterStandby(const PowerCaptureSample& sample, bool batteryOnly) {
    observe(sample, batteryOnly);
    awake.freeze();
    mode = PowerCaptureMode::Standby;
    standby = {};
    observe(sample, batteryOnly);
  }

  void exitStandby(const PowerCaptureSample& sample, bool batteryOnly) {
    observe(sample, batteryOnly);
    standby.freeze();
    mode = PowerCaptureMode::Awake;
    // Keep the pre-standby awake baseline frozen until a new unplugged cycle.
  }

  bool needsStandbyStart() const {
    return mode == PowerCaptureMode::Standby && standby.state == PowerWindowState::Empty;
  }

 private:
  PowerCaptureWindow& activeWindow() {
    switch (mode) {
      case PowerCaptureMode::Awake: return awake;
      case PowerCaptureMode::Standby: return standby;
    }
    return awake;  // Corrupt RAM is not a third capture mode.
  }
};

static_assert(sizeof(PowerCaptureSample) == 16);
static_assert(sizeof(PowerCaptureWindow) == 44);
static_assert(sizeof(PowerCapture) == 108);

}  // namespace plus_board
