// Diagnostic builds only (-DRICKYOS_NETCYCLE_DIAG=1): serial "CMD:NETCYCLE" walks the
// hotspot and join-network paths several times without touching the screen and logs the
// internal heap after every step, so a transfer refused for low memory can be traced to
// the step that took the memory and to whatever stays allocated after Wi-Fi is off.
#if RICKYOS_NETCYCLE_DIAG && !defined(SIMULATOR)

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HalMemory.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_sntp.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>

#include "NetworkStartup.h"
#include "WifiCredentialStore.h"
#include "network/CrossPointWebServer.h"

namespace {

void snap(const int round, const char* step) {
  const auto heap = HalMemory::getInternalHeap();
  LOG_INF("NETCYC", "round=%d %-16s internal free=%u largest=%u min=%u", round, step,
          static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.largestBlockBytes),
          static_cast<unsigned>(heap.minFreeBytes));
}

// Same order as CrossPointWebServerActivity::onExit() + finishWifiSessionWithoutRestart().
void wifiOff(const int round, const bool ap) {
  MDNS.end();
  if (ap) {
    WiFi.softAPdisconnect(true);
  } else {
    WiFi.disconnect(false);
  }
  delay(30);
  if (esp_sntp_enabled()) esp_sntp_stop();
  WiFi.mode(WIFI_OFF);
  delay(500);
  snap(round, ap ? "ap off" : "sta off");
}

void serve(const int round, const char* label) {
  auto server = std::make_unique<CrossPointWebServer>();
  const bool ok = server->begin();
  snap(round, ok ? label : "server REFUSED");
  server->stop();
}

void hotspotRound(GfxRenderer& renderer, const int round) {
  if (!NetworkStartup::setMode(renderer, WIFI_AP)) {
    LOG_ERR("NETCYC", "round=%d AP mode failed", round);
    return;
  }
  snap(round, "ap mode");
  WiFi.softAP("CrossPoint-Reader", nullptr, 1, false, 4);
  delay(100);
  snap(round, "ap started");
  MDNS.begin("crosspoint");
  auto dns = std::make_unique<DNSServer>();
  dns->setErrorReplyCode(DNSReplyCode::NoError);
  dns->start(53, "*", WiFi.softAPIP());
  snap(round, "ap dns+mdns");
  serve(round, "ap server");
  dns->stop();
  dns.reset();
  wifiOff(round, true);
}

void stationRound(GfxRenderer& renderer, const int round) {
  if (!NetworkStartup::setMode(renderer, WIFI_STA)) {
    LOG_ERR("NETCYC", "round=%d STA mode failed", round);
    return;
  }
  snap(round, "sta mode");
  const int found = WiFi.scanNetworks(false);
  snap(round, "sta scanned");
  WiFi.scanDelete();
  if (WIFI_STORE.getCredentialCount() == 0) WIFI_STORE.loadFromFile();
  auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!credential) credential = WIFI_STORE.getCredentialAt(0);
  if (credential) {
    WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
    for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; ++i) delay(100);
  }
  LOG_INF("NETCYC", "round=%d scan=%d saved=%d connected=%d", round, found, credential ? 1 : 0,
          WiFi.status() == WL_CONNECTED ? 1 : 0);
  snap(round, "sta joined");
  if (WiFi.status() == WL_CONNECTED) {
    MDNS.begin("crosspoint");
    serve(round, "sta server");
  }
  wifiOff(round, false);
}

// Every task with its stack placement and free stack, to see what the first Wi-Fi use leaves.
void listTasks(const char* when) {
  UBaseType_t count = uxTaskGetNumberOfTasks();
  auto tasks = std::make_unique<TaskStatus_t[]>(count + 4);
  count = uxTaskGetSystemState(tasks.get(), count + 4, nullptr);
  for (UBaseType_t i = 0; i < count; ++i) {
    const auto& t = tasks[i];
    const bool internal = esp_ptr_internal(t.pxStackBase);
    LOG_INF("NETCYC", "%s task %-16s stack=%s free_words=%u", when, t.pcTaskName, internal ? "internal" : "psram",
            static_cast<unsigned>(t.usStackHighWaterMark));
    delay(30);  // the USB console drops lines in a burst
  }
}

}  // namespace

void runNetCycleDiag(GfxRenderer& renderer) {
  constexpr int kRounds = 2;
  snap(0, "start");
  listTasks("before");
  for (int round = 1; round <= kRounds; ++round) {
    hotspotRound(renderer, round);
    stationRound(renderer, round);
  }
  snap(kRounds, "end");
  listTasks("after");
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  LOG_INF("NETCYC", "done");
}

#endif
