#pragma once

#include <Logging.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Plus radio owners may finish in a different order from acquisition (SNTP
// starts before the web server). Keep the original mode until the last owner
// leaves. The shared mutex and state use static storage, never heap memory.
class HalWifiAwakeLease {
  struct State {
    StaticSemaphore_t storage{};
    SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&storage);
    unsigned owners = 0;
    wifi_ps_type_t previous = WIFI_PS_MIN_MODEM;
  };
  static State& state() {
    static State value;
    return value;
  }
  bool held_ = false;

 public:
  HalWifiAwakeLease() = default;
  HalWifiAwakeLease(const HalWifiAwakeLease&) = delete;
  HalWifiAwakeLease& operator=(const HalWifiAwakeLease&) = delete;
  ~HalWifiAwakeLease() { release(); }

  bool acquire() {
    auto& s = state();
    if (!s.mutex || xSemaphoreTake(s.mutex, portMAX_DELAY) != pdTRUE) return false;
    if (!held_) {
      wifi_ps_type_t previous;
      const esp_err_t result = esp_wifi_get_ps(&previous);
      if (result != ESP_OK || esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
        LOG_ERR("WIFI", "Failed to acquire radio-awake lease");
        xSemaphoreGive(s.mutex);
        return false;
      }
      if (s.owners == 0) s.previous = previous;
      ++s.owners;
      held_ = true;
      LOG_INF("WIFI", "Radio awake acquired: owners=%u", s.owners);
    }
    xSemaphoreGive(s.mutex);
    return true;
  }

  void release() {
    if (!held_) return;
    auto& s = state();
    if (!s.mutex || xSemaphoreTake(s.mutex, portMAX_DELAY) != pdTRUE) return;
    if (held_) {
      held_ = false;
      --s.owners;
      if (s.owners == 0) {
        wifi_ps_type_t current;
        if (esp_wifi_get_ps(&current) == ESP_OK && current == WIFI_PS_NONE) {
          const esp_err_t result = esp_wifi_set_ps(s.previous);
          LOG_INF("WIFI", "Last radio owner released: restored=%u result=%d", s.previous, result);
        }
      } else {
        LOG_INF("WIFI", "Radio awake retained: owners=%u", s.owners);
      }
    }
    xSemaphoreGive(s.mutex);
  }

  static unsigned activeCount() {
    auto& s = state();
    if (!s.mutex || xSemaphoreTake(s.mutex, portMAX_DELAY) != pdTRUE) return 0;
    const unsigned count = s.owners;
    xSemaphoreGive(s.mutex);
    return count;
  }
};
