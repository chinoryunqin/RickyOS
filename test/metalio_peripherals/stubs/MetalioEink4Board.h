#pragma once
#include <cstdint>
#include <cstddef>
namespace freeink::metalio {
inline constexpr uint8_t CHARGER = 0x6b;
inline constexpr uint16_t USB_MUX_SEL = 1;
extern bool ready;
extern uint16_t output;
bool read(uint8_t, uint8_t, uint8_t*, size_t);
bool setOutput(uint16_t);
}
