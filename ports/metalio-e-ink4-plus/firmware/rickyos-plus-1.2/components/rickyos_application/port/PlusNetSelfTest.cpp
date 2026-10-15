// Unattended network self-test for the Plus (built only with RICKYOS_PLUS_NETTEST=1).
// 30 s after boot it joins the last used network the way WifiSelectionActivity does,
// then fetches the hosts RickyOS uses (COS, WeRead, jsDelivr/Fastly, the site) for three
// rounds and logs, per request, whether data arrived, how much and how fast. Two 1 MB
// reads measure throughput from COS and from Fastly. Nothing is written to the card.
#if RICKYOS_PLUS_NETTEST

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <string>

#include "HalWifiAwakeLease.h"
#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"

namespace {
volatile bool g_active = false;

struct Probe {
  const char* name;
  const char* url;
  size_t cap;  // stop reading after this many bytes
};

constexpr Probe kProbes[] = {
    {"cos-manifest", "https://rickyos-1309217881.cos.ap-guangzhou.myqcloud.com/fonts/cos.json", 16 * 1024},
    {"weread", "https://weread.qq.com/", 16 * 1024},
    {"fastly-manifest", "https://fastly.jsdelivr.net/gh/chinoryunqin/RickyOS-fonts@latest/mirror.json", 16 * 1024},
    {"site-ota", "https://chinoryunqin.github.io/RickyOS-site/ota.json", 16 * 1024},
    {"cos-1MB", "https://rickyos-1309217881.cos.ap-guangzhou.myqcloud.com/fonts/v1.0.2/RickySans_12.cpfont",
     1024 * 1024},
    {"fastly-1MB", "https://fastly.jsdelivr.net/gh/chinoryunqin/RickyOS-fonts@v1.0.2/fonts/RickySans_12.cpfont",
     1024 * 1024},
};

void run(const Probe& probe, int round) {
  size_t bytes = 0;
  const uint32_t started = millis();
  HttpDownloader::fetchUrl(probe.url, [&bytes, &probe](const uint8_t*, size_t len) {
    bytes += len;
    return bytes < probe.cap;  // false ends the transfer once enough has arrived
  });
  const uint32_t elapsed = millis() - started;
  LOG_INF("NETTEST", "round=%d %s bytes=%u ms=%lu rate=%lu KB/s", round, probe.name, static_cast<unsigned>(bytes),
          static_cast<unsigned long>(elapsed), static_cast<unsigned long>(elapsed ? bytes / elapsed : 0));
}

void task(void*) {
  vTaskDelay(pdMS_TO_TICKS(30000));
  g_active = true;  // main.cpp holds off auto-Standby (and its light sleep) meanwhile
  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  const auto credential = WIFI_STORE.findCredential(ssid);
  if (ssid.empty() || !credential) {
    LOG_ERR("NETTEST", "no saved network to join");
    g_active = false;
    vTaskDelete(nullptr);
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
#if RICKYOS_PLUS_NETTEST_AX
  // Comparison run: leave the driver's default protocols, 802.11ax included.
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX);
#else
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
#endif
  const uint32_t joinStarted = millis();
  WiFi.begin(ssid.c_str(), credential->password.c_str());
  while (WiFi.status() != WL_CONNECTED && millis() - joinStarted < 30000) vTaskDelay(pdMS_TO_TICKS(200));
  uint8_t protocols = 0;
  esp_wifi_get_protocol(WIFI_IF_STA, &protocols);
  wifi_phy_mode_t phy = WIFI_PHY_MODE_LR;
  esp_wifi_sta_get_negotiated_phymode(&phy);
  LOG_INF("NETTEST", "join status=%d after %lu ms rssi=%d protocols=0x%x phy=%d", static_cast<int>(WiFi.status()),
          static_cast<unsigned long>(millis() - joinStarted), static_cast<int>(WiFi.RSSI()), protocols,
          static_cast<int>(phy));
  if (WiFi.status() == WL_CONNECTED) {
    HalWifiAwakeLease awake;  // no modem power save, as the app's own downloads run
    awake.acquire();
    for (int round = 1; round <= 3; ++round) {
      for (const auto& probe : kProbes) run(probe, round);
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  LOG_INF("NETTEST", "done");
  g_active = false;
  vTaskDelete(nullptr);
}
}  // namespace

bool plusNetSelfTestActive() { return g_active; }

void plusNetSelfTestStart() {
  // wolfSSL handshakes need a deep stack; the task exits when the test ends.
  xTaskCreate(task, "nettest", 20 * 1024, nullptr, 3, nullptr);
}

#endif
