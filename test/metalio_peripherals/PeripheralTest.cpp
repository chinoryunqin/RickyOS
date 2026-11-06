#include <Wire.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "CameraPower.h"
#include "Nt26AtResponse.h"
#include "Nt26Frame.h"
#include "activities/apps/CameraPreviewRaster.h"

namespace freeink::metalio {
bool ready = true;
uint16_t output = 0xffff;
bool setOutput(uint16_t value) {
  output = value;
  return true;
}
}  // namespace freeink::metalio
namespace {
std::array<uint8_t, 256> regs;
std::vector<std::pair<uint8_t, uint8_t>> writes;
int failReg = -1, readFail = -1;
uint8_t reg = 0, value = 0;
bool first = true;
unsigned delayed = 0;
}  // namespace
FakeWire Wire;
void FakeWire::beginTransmission(uint8_t address) {
  assert(address == 0x6b);
  first = true;
}
void FakeWire::write(uint8_t byte) {
  if (first) {
    reg = byte;
    first = false;
  } else
    value = byte;
}
uint8_t FakeWire::endTransmission() {
  if (reg == failReg) {
    failReg = -1;
    return 1;
  }
  regs[reg] = value;
  writes.emplace_back(reg, value);
  return 0;
}
bool freeink::metalio::read(uint8_t address, uint8_t reg, uint8_t* out, size_t count) {
  assert(address == 0x6b && count == 1);
  if (reg == readFail) return false;
  *out = regs[reg];
  return true;
}
void delay(unsigned ms) { delayed += ms; }
extern "C" bool freeink_metalio_recover_camera_power();
int main() {
  using namespace freeink::metalio_camera;
  // Captured specification vectors: AT payload 3 bytes, seq 0; Ethernet
  // length 1500, seq 15, continue/XOFF set (checksum has an 8-bit carry).
  nt26::FrameHeader at{};
  at.SetPayloadLength(3);
  at.SetType(nt26::FrameType::kAtCommand);
  at.UpdateChecksum();
  const uint8_t atExpected[] = {0x03, 0x00, 0x04, 0x04};
  assert(memcmp(at.raw, atExpected, 4) == 0);
  const nt26::FrameHeader eth{{0xdc, 0xf5, 0x03, 0xd6}};
  assert(eth.ValidateChecksum() && eth.GetPayloadLength() == 1500 && eth.GetSequence() == 15);
  assert(eth.GetContinue() && eth.GetFlowControl() && eth.GetType() == nt26::FrameType::kEthernet);
  auto corrupt = eth;
  corrupt.raw[0] ^= 1;
  assert(!corrupt.ValidateChecksum());
  at.SetSequence(17);
  assert(at.GetSequence() == 1 && at.GetPayloadLength() == 3);
  puts("PASS NT26 independent wire vectors, carry checksum, corruption, sequence wrap");

  regs.fill(0x95);
  regs[0x18] = 0x05;
  regs[0x16] = 0xb5;
  regs[0x15] = 0xd5;
  const auto original = regs;
  assert(powerOn() == ESP_OK);
  assert(regs[0x0a] == 0x25 && regs[0x0b] == 0x93 && regs[0x0c] == 0xd5 && regs[0x0d] == 0x8f);
  assert(regs[0x16] == 0x84 && regs[0x15] == 0x95 && regs[0x18] == 0x45 && delayed >= 30);
  assert(selectCamera() && freeink::metalio::output == 0xfffe);
  assert(powerOn() == ESP_ERR_INVALID_STATE);
  assert(powerOff() && regs == original);
  assert(writes[writes.size() - 8].first == 0x18 && !(writes[writes.size() - 8].second & 0x40));
  puts("PASS camera boost voltage/current, unrelated bits, mux, re-entry refusal, exact restore");

  failReg = 0x0d;
  assert(powerOn() == ESP_FAIL && regs == original);
  puts("PASS camera partial boost failure rolls back all saved registers");
  readFail = 0x0b;
  writes.clear();
  assert(powerOn() == ESP_FAIL && writes.empty());
  readFail = -1;
  puts("PASS camera failed initial read performs no register write");
  regs[0x18] |= 0x40;
  writes.clear();
  assert(powerOn() == ESP_ERR_INVALID_STATE && writes.empty());
  regs = original;
  puts("PASS existing OTG owner is preserved");
  assert(powerOn() == ESP_OK);
  assert(freeink_metalio_recover_camera_power() && regs == original);
  puts("PASS retained snapshot recovery restores charger after MCU reset");
  assert(powerOn() == ESP_OK);
  failReg = 0x18;
  writes.clear();
  assert(!powerOff() && (regs[0x18] & 0x40) && writes.empty());
  assert(freeink_metalio_recover_camera_power() && regs == original);
  puts("PASS failed VBUS disable does not restore charger fields");
  assert(powerOn() == ESP_OK);
  failReg = 0x0a;
  assert(!powerOff() && powerOn() == ESP_ERR_INVALID_STATE);
  assert(freeink_metalio_recover_camera_power() && regs == original);
  puts("PASS failed charger restoration retains recovery snapshot and rejects re-entry");
  std::array<uint8_t, 6> guarded{0xaa, 0, 0, 0, 0, 0xbb};
  const uint8_t blackWhite[] = {0, 255};
  cameraPreview::grayBlock({guarded.data() + 1, 8, 4, 2, 1}, blackWhite, 0, 0, 2, 2, 1);
  for (int i = 1; i < 5; ++i) assert(guarded[i] == 0xf0);
  assert(guarded[0] == 0xaa && guarded[5] == 0xbb);
  const uint8_t white[] = {255, 255};
  cameraPreview::grayBlock({guarded.data() + 1, 8, 4, 2, 1}, white, 0, 0, 2, 2, 1);
  for (int i = 1; i < 5; ++i) assert(guarded[i] == 0);
  cameraPreview::grayBlock({guarded.data() + 1, 8, 4, 2, 1}, blackWhite, -1, -1, 2, 2, 1);
  assert(guarded[0] == 0xaa && guarded[5] == 0xbb);
  puts("PASS preview raster scaling, black/white polarity, clipped source blocks and buffer guards");
  using nt26::AtFinal;
  assert(nt26::finalLine("\r\n+ECSIMCFG: \"SimSlot\",1\r\nO") == AtFinal::Pending);
  assert(nt26::finalLine("\r\n+ECSIMCFG: \"SimSlot\",1\r\nOK\r\n") == AtFinal::Ok);
  assert(nt26::finalLine("\r\n+COPS: 0,0,\"TOKYO\",7\r\n") == AtFinal::Pending);
  assert(nt26::finalLine("\r\n+CME ERROR: 10\r\n") == AtFinal::Error);
  assert(nt26::finalLine("\r\nERROR\r\n") == AtFinal::Error);
  puts("PASS multiline/fragmented AT reply completion and embedded OK text");
  return 0;
}
