#include <array>
#include <cassert>
#include <cstdio>

#include "driver/CrossMuxSsd1677Driver.cpp"

using namespace freeink;

void emit(const EpdBus& bus) {
  for (const auto& event : bus.events) {
    const uint16_t command = static_cast<uint16_t>(event.cmd);
    const uint32_t size = event.bytes.size();
    std::fwrite(&command, sizeof(command), 1, stdout);
    std::fwrite(&size, sizeof(size), 1, stdout);
    if (size) std::fwrite(event.bytes.data(), 1, size, stdout);
  }
}

int main() {
  EpdBus bus;
  CrossMuxSsd1677Driver driver(crossmuxSsd1677MetalioConfig());
  std::array<uint8_t, 128> raster;
  std::array<uint8_t, 32> bw;
  uint32_t random = 0x67214abc;
  driver.begin(bus);
  emit(bus);
  bus.clear();
  const auto submit = [&](bool clean = false) {
    bw.fill(0);
    for (unsigned pixel = 0; pixel < raster.size() * 2; ++pixel) {
      const unsigned level = (raster[pixel / 2] >> ((pixel & 1) * 4)) & 15;
      if (level >= 13) bw[pixel / 8] |= 0x80 >> (pixel % 8);
    }
    assert(driver.displayUiGrayscale(bus, raster.data(), bw.data(), clean));
    emit(bus);
    bus.clear();
  };
  for (unsigned frame = 0; frame < 16; ++frame) {
    for (auto& byte : raster) {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      byte = random;
    }
    submit(frame == 6);
    submit();  // Identical frame must produce the same empty transaction.
  }
  driver.requestResync(1);
  submit();
  driver.display(bus, bw.data(), nullptr, RefreshMode::Fast, false);
  emit(bus);
  bus.clear();
  submit();
  for (unsigned phase = 1; phase <= 2; ++phase) {
    raster[0] ^= 0xff;
    bus.failAt = bus.activation + phase;
    assert(!driver.displayUiGrayscale(bus, raster.data(), bw.data(), false));
    emit(bus);
    bus.failAt = 0;
    bus.busy = false;
    bus.clear();
    submit();
  }
}
