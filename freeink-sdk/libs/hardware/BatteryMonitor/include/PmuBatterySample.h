#pragma once
#include <cstdint>

namespace freeink {
namespace battery {
constexpr uint16_t SOC_UNKNOWN = 0xFFFF;
// Read Pico PMU 1.0.8 documented ADC validity range, not a protection setting.
inline bool validPackMillivolts(uint16_t mv) { return mv >= 2700 && mv <= 4400; }

// Official PMU reference curve, NOT a measured/coulomb-counted SOC. Manufacturer
// documents approx. +/-10 percentage points; charge/latch history is not recreated.
// MindReset/read_pico_firmware 6a247cc9, components/read_pico_pmu/docs/
// llms-full_zh_cn.md, "电池". Independent implementation of the documented nodes.
inline uint16_t readPicoVoltagePermille(uint16_t mv) {
  struct Point {
    uint16_t mv, permille;
  };
  constexpr Point points[] = {{3050, 0},   {3150, 20},  {3300, 50},  {3450, 70},  {3550, 110}, {3600, 200}, {3650, 330},
                              {3700, 450}, {3800, 620}, {3900, 780}, {4000, 900}, {4100, 960}, {4200, 1000}};
  if (mv <= points[0].mv) return 0;
  for (unsigned i = 1; i < sizeof(points) / sizeof(points[0]); ++i) {
    if (mv <= points[i].mv) {
      const uint32_t span = points[i].mv - points[i - 1].mv;
      return points[i - 1].permille +
             ((mv - points[i - 1].mv) * uint32_t(points[i].permille - points[i - 1].permille) + span / 2) / span;
    }
  }
  return 1000;
}

inline uint16_t readPicoVoltagePercent(uint16_t mv) { return (readPicoVoltagePermille(mv) + 5u) / 10u; }

struct PmuChargeState {
  bool known = false;
  bool charging = false;
  bool externalPower = false;
  bool fullInferred = false;
};

inline PmuChargeState readPicoChargeState(uint8_t charge) {
  switch (charge) {
    case 1:
      return {true, false, false, false};  // NOT_CHARGING, per PMU's plug detection.
    case 2:
      return {true, true, true, false};  // CHARGING.
    case 3:
      return {true, false, true, true};  // FULL_INFERRED != charging current.
    default:
      return {};  // UNKNOWN, FAULT, reserved: not evidence of unplugging.
  }
}
}  // namespace battery
}  // namespace freeink
