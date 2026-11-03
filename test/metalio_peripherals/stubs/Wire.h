#pragma once
#include <cstdint>
struct FakeWire {
  void beginTransmission(uint8_t);
  void write(uint8_t);
  uint8_t endTransmission();
};
extern FakeWire Wire;
