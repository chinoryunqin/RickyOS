#include "PlusBoard.h"
#include "PlusAudio.h"
#include <InputManager.h>
#include <PowerManager.h>
#include <plus_i2c_wire.h>
#include <metalio_i2c1.h>
#include <epd_board_metalio_eink4_plus.h>
#include <config.h>
#include <esp_log.h>
namespace plus_board {
namespace {
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
  epd_board_metalio_eink4_plus_pwr_key_pulse_train();
  ESP_LOGW("PlusBoard", "shutdown pulse complete; waiting for external power controller");
}
}
bool begin() {
  if (plus_i2c_begin() != ESP_OK) return false;
  if (!epd_board_metalio_eink4_plus_safe_io_init()) return false;
  epd_board_metalio_eink4_plus_tp_reset();
  InputManager::setButtonHook(buttons);
  freeink::PowerManager::setHostShutdownHook(shutdown);
  return true;
}
}
