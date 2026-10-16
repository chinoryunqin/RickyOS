// Real download/SD/clock paths, enabled only for unattended hardware acceptance.
#if RICKYOS_PLUS_NETWORK_ACCEPTANCE
#include <Arduino.h>
#include <Logging.h>
#include <Network.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <cstdio>
#include <cstring>

#include "HalClock.h"
#include "HalPowerManager.h"
#include "HalStorage.h"
#include "HalWifiAwakeLease.h"
#include "network/HttpDownloader.h"

extern void plusTcpTraceBegin();
extern void plusTcpTraceEnd();

namespace {
constexpr char cosHost[] = "rickyos-1309217881.cos.ap-guangzhou.myqcloud.com";
struct Download {
  const char* name;
  const char* url;
  uint32_t bytes;
  uint32_t crc;
  bool index;
};
constexpr Download wallpaper{"wallpapers", "https://rickyos-1309217881.cos.ap-guangzhou.myqcloud.com/wallpapers/cos.json",
                             3965, 252721562, true};
constexpr Download fonts{"fonts", "https://rickyos-1309217881.cos.ap-guangzhou.myqcloud.com/fonts/cos.json",
                         9024, 2970146193u, true};
constexpr Download font{"font12", "https://rickyos-1309217881.cos.ap-guangzhou.myqcloud.com/fonts/v1.0.2/RickySans_12.cpfont",
                        1763452, 2837099957u, false};

uint32_t updateCrc(uint32_t crc, const uint8_t* bytes, size_t length) {
  while (length--) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return crc;
}

bool restartNetwork(const wifi_config_t& original, uint8_t protocols, const char* name) {
  const uint32_t started = millis();
  wifi_config_t config = original;  // SDK setter is mutable; keep original intact.
  if (!WiFi.mode(WIFI_OFF) || !WiFi.mode(WIFI_STA) ||
      esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_wifi_set_protocol(WIFI_IF_STA, protocols) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK || !WiFi.reconnect()) {
    LOG_ERR("ACCEPT", "name=%s restart_failed", name);
    return false;
  }
  while (WiFi.status() != WL_CONNECTED && millis() - started < 35000) delay(50);
  const bool joined = WiFi.status() == WL_CONNECTED;
  const bool route = joined && Network.setDefaultInterface(WiFi.STA);
  LOG_INF("ACCEPT", "name=%s joined=%d route=%d join_ms=%lu rssi=%d", name, joined, route,
          static_cast<unsigned long>(millis() - started), joined ? static_cast<int>(WiFi.RSSI()) : -127);
  return route;
}

bool reserveTemp(const char* name, char* path, size_t capacity) {
  for (unsigned id = 0; id < 16; ++id) {
    const int size = snprintf(path, capacity, "/.crosspoint/plus-net-accept-20261016-%s-%u.tmp", name, id);
    if (size <= 0 || static_cast<size_t>(size) >= capacity) return false;
    // Exclusive creation gives ownership; never truncate an existing user file.
    auto reserved = Storage.open(path, O_CREAT | O_EXCL | O_WRITE);
    if (reserved) return true;  // Scope destructor closes before downloader opens.
  }
  return false;
}

bool download(const Download& item) {
  char path[96];
  if (!reserveTemp(item.name, path, sizeof(path))) {
    LOG_ERR("ACCEPT", "name=%s temp_reservation_failed", item.name);
    return false;
  }
  struct Context {
    uint32_t started;
    size_t lastProgress = 0;
    bool cancel = false;
    bool onlyCos = true;
  } context{millis()};
  plusTcpTraceBegin();
  const auto result = item.index ? HttpDownloader::downloadIndex(item.url, path)
      : HttpDownloader::downloadToFile(item.url, path,
          [&context](size_t bytes, size_t total) {
            if (bytes >= context.lastProgress && bytes - context.lastProgress >= 256 * 1024) {
              context.lastProgress = bytes;
              LOG_INF("ACCEPT", "font_progress bytes=%u total=%u ms=%lu", static_cast<unsigned>(bytes),
                      static_cast<unsigned>(total), static_cast<unsigned long>(millis() - context.started));
            }
          }, &context.cancel, "", "", {}, false,
          [&context]() { if (millis() - context.started >= 90000) context.cancel = true; },
          [](const char* host, void* state) {
            auto* value = static_cast<Context*>(state);
            value->onlyCos = value->onlyCos && host && std::strcmp(host, cosHost) == 0;
          }, &context);
  const uint32_t elapsed = millis() - context.started;
  plusTcpTraceEnd();
  uint32_t bytes = 0, crc = 0xffffffffu;
  bool readOk = false;
  {
    HalFile file;
    if (Storage.openFileForRead("ACCEPT", path, file)) {
      uint8_t buffer[1024];  // Streaming integrity check, no full-font allocation.
      for (;;) {
        const int count = file.read(buffer, sizeof(buffer));
        if (count == 0) { readOk = true; break; }
        if (count < 0 || static_cast<size_t>(count) > sizeof(buffer)) break;
        bytes += count;
        crc = updateCrc(crc, buffer, count);
      }
    }
  }
  crc ^= 0xffffffffu;
  const bool match = readOk && bytes == item.bytes && crc == item.crc;
  const bool cleanup = !Storage.exists(path) || Storage.remove(path);
  int8_t power = 0;
  const esp_err_t powerRead = esp_wifi_get_max_tx_power(&power);
  LOG_INF("ACCEPT", "name=%s result=%d download_ms=%lu bytes=%lu crc32=%lu content_match=%d cleanup=%d only_cos=%d",
          item.name, static_cast<int>(result), static_cast<unsigned long>(elapsed),
          static_cast<unsigned long>(bytes), static_cast<unsigned long>(crc), match, cleanup,
          item.index ? -1 : static_cast<int>(context.onlyCos));
  LOG_INF("ACCEPT", "name=%s after_power_read=%d after_power=%d radio_owners=%u", item.name, powerRead, power,
          HalWifiAwakeLease::activeCount());
  return result == HttpDownloader::OK && match && cleanup && (item.index || context.onlyCos);
}
}

void plusNetworkAcceptanceOnConnected() {
  HalPowerManager::Lock performance;
  halClock.cancelSync();  // Each test starts without another radio owner/warmup.
  wifi_config_t original{};
  uint8_t protocols = 0;
  if (esp_wifi_get_config(WIFI_IF_STA, &original) != ESP_OK ||
      esp_wifi_get_protocol(WIFI_IF_STA, &protocols) != ESP_OK) {
    LOG_ERR("ACCEPT", "configuration_unavailable");
    return;
  }
  const bool reconnect = WiFi.getAutoReconnect();
  WiFi.setAutoReconnect(false);
  LOG_INF("ACCEPT", "begin cold_downloads=4 native_clock=1");
  unsigned passed = 0;
  for (const auto* item : {&wallpaper, &fonts, &font}) {
    if (restartNetwork(original, protocols, item->name) && download(*item)) ++passed;
  }
  if (restartNetwork(original, protocols, "clock")) {
    const uint32_t started = millis();
    const bool began = halClock.requestSync();
    while (began && halClock.syncState() == ClockSyncState::Syncing && millis() - started < 55000) {
      halClock.update();
      delay(50);
    }
    const bool success = began && halClock.syncState() == ClockSyncState::Succeeded;
    const auto epoch = halClock.nowUtc();
    halClock.cancelSync();
    LOG_INF("ACCEPT", "clock began=%d success=%d valid_time=%d epoch=%lld ms=%lu radio_owners=%u", began, success,
            halClock.hasValidTime(), static_cast<long long>(epoch),
            static_cast<unsigned long>(millis() - started), HalWifiAwakeLease::activeCount());
  }
  if (restartNetwork(original, protocols, "wallpapers-repeat") && download(wallpaper)) ++passed;
  WiFi.setAutoReconnect(reconnect);
  LOG_INF("ACCEPT", "done downloads_passed=%u expected=4 auto_reconnect_restored=%d", passed,
          WiFi.getAutoReconnect() == reconnect);
}
#endif
