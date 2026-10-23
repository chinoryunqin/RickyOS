// BQ27220 configuration backup (diagnostic builds only, RICKYOS_PLUS_GAUGE_DUMP=1).
//
// Read-only: unseal, read the data memory blocks that hold calibration,
// configuration and the CEDV gas-gauging profile, log them as hex, then seal
// again. No ENTER_CFG_UPDATE, no MACData write, so the gauge configuration is
// unchanged. Sequence and addresses follow TI SLUUBD4A and the open BQ27220
// driver in flipperzero-firmware (lib/drivers/bq27220*.{c,h}).
#include "PlusGaugeDump.h"

#if defined(RICKYOS_PLUS_GAUGE_DUMP)
#include <Arduino.h>
#include <esp_log.h>
#include <metalio_i2c1.h>
#include <plus_i2c_wire.h>

namespace plus_board {
namespace {
constexpr const char* TAG = "PlusGauge";
constexpr uint8_t kAddr = 0x55;
constexpr uint16_t kUnsealKey1 = 0x0414, kUnsealKey2 = 0x3672;
constexpr uint16_t kSealed = 0x0030;

bool control(uint16_t subcommand) {
  const uint8_t data[2] = {uint8_t(subcommand & 0xff), uint8_t(subcommand >> 8)};
  const bool ok = plus_i2c_write(kAddr, 0x00, data, 2) == ESP_OK;
  delay(5);
  return ok;
}

bool read16(uint8_t reg, uint16_t& value) {
  uint8_t raw[2]{};
  if (plus_i2c_read(kAddr, reg, raw, 2) != ESP_OK) return false;
  value = uint16_t(raw[0]) | uint16_t(raw[1]) << 8;
  return true;
}

// Control() subcommand read-back: write the subcommand, then read 0x40 (MACData).
bool controlRead(uint16_t subcommand, uint16_t& value) {
  const uint8_t data[2] = {uint8_t(subcommand & 0xff), uint8_t(subcommand >> 8)};
  if (plus_i2c_write(kAddr, 0x3e, data, 2) != ESP_OK) return false;
  delay(2);
  uint8_t raw[2]{};
  if (plus_i2c_read(kAddr, 0x40, raw, 2) != ESP_OK) return false;
  value = uint16_t(raw[0]) | uint16_t(raw[1]) << 8;
  return true;
}

bool readBlock(uint16_t address, uint8_t (&out)[32]) {
  const uint8_t select[2] = {uint8_t(address & 0xff), uint8_t(address >> 8)};
  if (plus_i2c_write(kAddr, 0x3e, select, 2) != ESP_OK) return false;
  delay(2);
  return plus_i2c_read(kAddr, 0x40, out, sizeof(out)) == ESP_OK;
}

void logStandard() {
  struct Reg {
    uint8_t reg;
    const char* name;
  };
  constexpr Reg regs[] = {{0x08, "Voltage"},        {0x0c, "Current"},      {0x06, "Temperature"},
                          {0x10, "RemainingCap"},   {0x12, "FullChargeCap"}, {0x2c, "StateOfCharge"},
                          {0x3c, "DesignCapacity"}, {0x3a, "OperationStatus"}, {0x0a, "BatteryStatus"},
                          {0x22, "RawCoulombCount"}, {0x2a, "CycleCount"}};
  for (const auto& r : regs) {
    uint16_t value = 0;
    if (read16(r.reg, value))
      ESP_LOGI(TAG, "std %s(0x%02x)=0x%04x (%u)", r.name, r.reg, value, value);
    else
      ESP_LOGW(TAG, "std %s(0x%02x) read failed", r.name, r.reg);
  }
  uint16_t device = 0, firmware = 0;
  if (controlRead(0x0001, device)) ESP_LOGI(TAG, "DEVICE_NUMBER=0x%04x", device);
  if (controlRead(0x0002, firmware)) ESP_LOGI(TAG, "FW_VERSION=0x%04x", firmware);
}
}  // namespace

void dumpGaugeConfig() {
  if (!metalio_i2c1_lock(200)) {
    ESP_LOGW(TAG, "dump: I2C busy, skipped");
    return;
  }
  logStandard();
  uint16_t status = 0;
  read16(0x3a, status);
  ESP_LOGI(TAG, "dump: OperationStatus before unseal=0x%04x", status);
  const bool unsealed = control(kUnsealKey1) && control(kUnsealKey2);
  read16(0x3a, status);
  ESP_LOGI(TAG, "dump: unseal sent=%d OperationStatus=0x%04x", unsealed, status);
  // Calibration (0x9180..), configuration (0x9200..) and the CEDV profile (0x9280..0x92ff).
  for (uint16_t address = 0x9180; address < 0x9300; address += 32) {
    uint8_t block[32]{};
    if (!readBlock(address, block)) {
      ESP_LOGW(TAG, "dm %04x read failed", address);
      continue;
    }
    char hex[32 * 3 + 1];
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + i * 3, 4, "%02x ", block[i]);
    ESP_LOGI(TAG, "dm %04x: %s", address, hex);
  }
  // Back to SEALED, as the gauge was before this dump.
  const bool sealed = control(kSealed);
  read16(0x3a, status);
  ESP_LOGI(TAG, "dump: seal sent=%d OperationStatus=0x%04x (no data memory written)", sealed, status);
  metalio_i2c1_unlock();
}
}  // namespace plus_board
#else
namespace plus_board {
void dumpGaugeConfig() {}
}  // namespace plus_board
#endif
