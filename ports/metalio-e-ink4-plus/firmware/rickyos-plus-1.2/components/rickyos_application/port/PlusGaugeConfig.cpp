// BQ27220 pack configuration for the Plus (RICKYOS_PLUS_GAUGE_CONFIG builds).
//
// The gauge ships with TI's ROM defaults (Design/Full Charge Capacity 3000 mAh,
// EDV0/1/2 3031/3385/3501 mV; read on device 2026-10-23, gauge1 dump). Its data
// memory is RAM: the values survive MCU resets and power-off while the cell stays
// connected, and return to the defaults when the pack is disconnected. So at boot:
// if DesignCapacity() is not ours, write the pack values once and REINIT.
//
// Pack: manufacturer 424860-2000 record, 1000 mA to 3.3 V = 1870.7 mAh
// (PlusBatteryEstimator.h). Empty at 3.3 V, matching that measurement.
// Sequence and addresses: TI SLUUBD4A; open driver flipperzero-firmware
// lib/drivers/bq27220.c (unseal, full access, ENTER_CFG_UPDATE, MACData + sum/len,
// EXIT_CFG_UPDATE_REINIT, seal). Stack only, no allocation.
#include "PlusGaugeConfig.h"

#include <Arduino.h>
#include <esp_log.h>
#include <metalio_i2c1.h>
#include <plus_i2c_wire.h>

#include <atomic>

namespace plus_board {
namespace {
constexpr const char* TAG = "PlusGauge";
constexpr uint8_t kAddr = 0x55;
constexpr uint16_t kUnsealKey1 = 0x0414, kUnsealKey2 = 0x3672, kFullAccessKey = 0xffff;
constexpr uint16_t kEnterCfgUpdate = 0x0090, kExitCfgUpdateReinit = 0x0091, kSealed = 0x0030;
constexpr uint16_t kCfgUpdateBit = 1u << 10, kInitCompBit = 1u << 5;

constexpr uint16_t kPackMah = 1871;
struct Field {
  uint16_t address;
  uint16_t value;  // big-endian in data memory
  const char* name;
};
constexpr Field kFields[] = {
    {0x929d, kPackMah, "FullChargeCapacity"},
    {0x929f, kPackMah, "DesignCapacity"},
    {0x92b4, 3300, "EDV0"},
    {0x92b7, 3400, "EDV1"},
    {0x92ba, 3500, "EDV2"},
};

std::atomic<bool> configured{false};

bool read16(uint8_t reg, uint16_t& value) {
  uint8_t raw[2]{};
  if (plus_i2c_read(kAddr, reg, raw, 2) != ESP_OK) return false;
  value = uint16_t(raw[0]) | uint16_t(raw[1]) << 8;
  return true;
}

bool write16(uint8_t reg, uint16_t value) {
  const uint8_t data[2] = {uint8_t(value & 0xff), uint8_t(value >> 8)};
  return plus_i2c_write(kAddr, reg, data, 2) == ESP_OK;
}

bool control(uint16_t subcommand) {
  const bool ok = write16(0x00, subcommand);
  delay(5);
  return ok;
}

bool waitStatus(uint16_t mask, bool set, uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (uint32_t(millis() - started) < timeoutMs) {
    uint16_t status = 0;
    if (read16(0x3a, status) && ((status & mask) != 0) == set) return true;
    delay(20);
  }
  return false;
}

bool readField(uint16_t address, uint16_t& value) {
  if (!write16(0x3e, address)) return false;
  delay(2);
  uint8_t raw[2]{};
  if (plus_i2c_read(kAddr, 0x40, raw, 2) != ESP_OK) return false;
  value = uint16_t(raw[0]) << 8 | raw[1];
  return true;
}

bool writeField(const Field& field) {
  // 0x3E/0x3F: address (LE); 0x40..: data (BE). Then 0x60 checksum, 0x61 length.
  const uint8_t block[4] = {uint8_t(field.address & 0xff), uint8_t(field.address >> 8), uint8_t(field.value >> 8),
                            uint8_t(field.value & 0xff)};
  if (plus_i2c_write(kAddr, 0x3e, block, sizeof(block)) != ESP_OK) return false;
  delay(1);
  uint8_t sum = 0;
  for (uint8_t byte : block) sum = uint8_t(sum + byte);
  const uint8_t tail[2] = {uint8_t(0xff - sum), uint8_t(sizeof(block) + 2)};
  if (plus_i2c_write(kAddr, 0x60, tail, sizeof(tail)) != ESP_OK) return false;
  delay(10);
  return true;
}

bool fieldsMatch() {
  for (const auto& field : kFields) {
    uint16_t value = 0;
    if (!readField(field.address, value) || value != field.value) return false;
  }
  return true;
}
}  // namespace

bool configureGauge() {
  if (!metalio_i2c1_lock(500)) {
    ESP_LOGW(TAG, "config: I2C busy, skipped");
    return false;
  }
  uint16_t design = 0, status = 0;
  const bool designOk = read16(0x3c, design);
  read16(0x3a, status);
  bool ok = false;
  if (designOk && design == kPackMah) {
    // Already ours (survives resets while the pack stays connected). The EDVs are
    // checked once per boot too; DM reads need UNSEALED.
    control(kUnsealKey1);
    control(kUnsealKey2);
    ok = fieldsMatch();
    control(kSealed);
    ESP_LOGI(TAG, "config: pack values present (design=%u op=0x%04x) verified=%d", design, status, ok);
    if (ok) {
      configured.store(true);
      metalio_i2c1_unlock();
      return true;
    }
  }
  ESP_LOGI(TAG, "config: writing pack values (design was %u, op=0x%04x)", design, status);
  control(kUnsealKey1);
  control(kUnsealKey2);
  control(kFullAccessKey);
  control(kFullAccessKey);
  bool step = write16(0x3e, kEnterCfgUpdate) && waitStatus(kCfgUpdateBit, true, 2000);
  if (!step) ESP_LOGE(TAG, "config: CFGUPDATE not entered");
  for (const auto& field : kFields) {
    if (!step) break;
    step = writeField(field);
    ESP_LOGI(TAG, "config: %s(0x%04x)=%u written=%d", field.name, field.address, field.value, step);
  }
  // Always leave CONFIG UPDATE, even after a failed write; REINIT applies the values.
  write16(0x3e, kExitCfgUpdateReinit);
  delay(2000);
  const bool exited = waitStatus(kCfgUpdateBit, false, 3000) && waitStatus(kInitCompBit, true, 3000);
  ok = step && exited && fieldsMatch();
  control(kSealed);
  read16(0x3c, design);
  read16(0x3a, status);
  ESP_LOGI(TAG, "config: done ok=%d exited=%d design=%u op=0x%04x", ok, exited, design, status);
  configured.store(ok && design == kPackMah);
  metalio_i2c1_unlock();
  return configured.load();
}

bool gaugeConfigured() { return configured.load(); }
}  // namespace plus_board
