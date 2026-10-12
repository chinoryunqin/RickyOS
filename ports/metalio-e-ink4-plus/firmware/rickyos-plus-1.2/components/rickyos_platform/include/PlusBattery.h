#pragma once
#include <cstdint>
namespace plus_board {
// Match the OEM Plus pack's voltage endpoints, rather than the X4 ADC curve.
// This is a voltage estimate, not the gauge's learned state of charge.
constexpr bool batteryPercentFromMv(uint16_t mv, uint16_t& percent) {
  if (mv < 2500 || mv > 5000) return false;
  percent = mv <= 3300 ? 0 : mv >= 4330 ? 100 : ((mv - 3300) * 100u + 515u) / 1030u;
  return true;
}
}
