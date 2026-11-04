#include "HalCellular.h"

#include "MetalioFeatures.h"
#if RICKYOS_METALIO_CELLULAR_ENABLED
#include <HalPowerManager.h>
#include <Logging.h>
#include <Network.h>
#include <NetworkClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <freertos/idf_additions.h>
#include <uart_eth_modem.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace HalCellular {
namespace {
Snapshot current;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> working{false}, cancel{false}, wanted{false}, persistPending{false};
enum class Action { Connect, Disconnect, Switch, Refresh, Probe };
Action action = Action::Refresh;
uint8_t requestedSlot = 255;
std::atomic<bool> transportStarted{false};  // only the serialized control worker owns the driver lifecycle

UartEthModem& modem() {
  // Static control object; DMA buffers/queues/tasks exist only while enabled.
  // Vendor DMA pool: 4 x 1600 bytes; worker stacks: 4096 + 4096 + 3072 bytes.
  static UartEthModem::Config config = {.uart_num = UART_NUM_1,
                                        .baud_rate = 2000000,
                                        .tx_pin = GPIO_NUM_12,
                                        .rx_pin = GPIO_NUM_11,
                                        .mrdy_pin = GPIO_NUM_21,
                                        .srdy_pin = GPIO_NUM_5,
                                        .rx_buffer_count = 4,
                                        .rx_buffer_size = 1600};
  static UartEthModem instance(config);
  return instance;
}
void setState(State state, const char* error = "") {
  portENTER_CRITICAL(&stateMux);
  current.state = state;
  snprintf(current.error, sizeof(current.error), "%s", error);
  if (state != State::Connected) current.ip[0] = 0;
  portEXIT_CRITICAL(&stateMux);
}
void onEvent(UartEthModem::UartEthModemEvent event, const std::string& detail) {
  using E = UartEthModem::UartEthModemEvent;
  switch (event) {
    case E::Connected: {
      esp_netif_ip_info_t ip{};
      char address[40]{};
      if (modem().GetNetif() && esp_netif_get_ip_info(modem().GetNetif(), &ip) == ESP_OK)
        snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
      if (!address[0] || ip.ip.addr == 0) break;
      portENTER_CRITICAL(&stateMux);
      current.state = State::Connected;
      snprintf(current.ip, sizeof(current.ip), "%s", address);
      current.error[0] = 0;
      portEXIT_CRITICAL(&stateMux);
      break;
    }
    case E::ErrorNoSim:
      setState(State::NoSim);
      break;
    case E::ErrorRegistrationDenied:
      setState(State::Denied);
      break;
    case E::ErrorInitFailed:
      setState(State::Error, detail.c_str());
      break;
    case E::Connecting:
    case E::ErrorNoCarrier:
      setState(State::Searching);
      break;
    case E::Disconnected:
      if (wanted.load()) setState(State::Searching);
      break;
    default:
      break;
  }
}
void persist() {
  Preferences prefs;
  if (!prefs.begin("rickcell", false)) return;
  prefs.putBool("enabled", wanted.load());
  const auto value = snapshot();
  if (value.simSlot < 2) prefs.putUChar("slot", value.simSlot);
  prefs.end();
}
bool stopTransport() {
  if (!transportStarted) return true;
  if (modem().IsInitialized()) {
    std::string response;
    modem().SendAt("AT+CFUN=4", response, 2000);
  }
  const esp_err_t result = modem().Stop();
  if (result != ESP_OK) {
    setState(State::Error, "Modem worker shutdown timed out");
    return false;  // retain the static driver and all resources owned by live workers
  }
  transportStarted = false;
  return true;
}
bool startTransport(UartEthModem::StartMode mode, uint32_t budget) {
  if (!Network.begin()) {
    setState(State::Error, "Network initialization failed");
    return false;
  }
  modem().SetNetworkEventCallback(onEvent);
  if (modem().Start(mode) != ESP_OK) {
    setState(State::Error, "Modem transport could not start");
    return false;
  }
  transportStarted = true;
  const uint32_t started = millis();
  while (!modem().IsInitialized() && !cancel.load() && millis() - started < budget) {
    if (snapshot().state == State::Error) break;
    delay(50);
  }
  if (cancel.load()) return false;
  if (!modem().IsInitialized()) {
    setState(State::Error, "Modem initialization timed out");
    stopTransport();
    return false;
  }
  return true;
}
void updateInfo() {
  if (!modem().IsInitialized()) return;
  const int signal = modem().GetSignalStrength();
  const auto carrier = modem().GetCarrierName();
  std::string response;
  int slot = -1;
  if (modem().SendAt("AT+ECSIMCFG?", response, 1500) == ESP_OK) {
    LOG_INF("CELL", "SIM config: %.384s", response.c_str());
    const auto pos = response.find("SimSlot");
    if (pos != std::string::npos) {
      for (size_t i = pos + 7; i < response.size(); ++i)
        if (response[i] == '0' || response[i] == '1') {
          slot = response[i] - '0';
          break;
        }
    }
  }
  portENTER_CRITICAL(&stateMux);
  current.signal = signal;
  snprintf(current.carrier, sizeof(current.carrier), "%s", carrier.c_str());
  if (slot >= 0) current.simSlot = static_cast<uint8_t>(slot);
  portEXIT_CRITICAL(&stateMux);
}
void worker(void*) {
  const Action job = action;
  const uint8_t slot = requestedSlot;
  {
    HalPowerManager::Lock power;
    if (job == Action::Disconnect) {
      if (stopTransport()) setState(State::Off);
    } else if (job == Action::Probe) {
      NetworkClient client;
      client.setTimeout(5000);
      const bool tcp = connected() && client.connect("example.com", 80, 5000);
      char status[96]{};
      if (tcp) {
        client.print("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
        const uint32_t started = millis();
        while (!client.available() && client.connected() && millis() - started < 5000) delay(10);
        const size_t n = client.readBytesUntil('\n', status, sizeof(status) - 1);
        status[n] = 0;
      }
      client.stop();
      LOG_INF("CELL", "Data probe tcp=%u http=%s", tcp, status);
    } else if (job == Action::Refresh) {
      updateInfo();
    } else {
      if (job == Action::Switch) {
        if (stopTransport() && startTransport(UartEthModem::StartMode::kFlight, 35000)) {
          std::string response;
          char command[48];
          snprintf(command, sizeof(command), "AT+ECSIMCFG=\"SimSlot\",%u", slot);
          if (modem().SendAt(command, response, 5000) == ESP_OK) {
            portENTER_CRITICAL(&stateMux);
            current.simSlot = slot;
            portEXIT_CRITICAL(&stateMux);
            modem().SendAt("AT+CFUN=1,1", response, 5000);
            stopTransport();
            delay(2000);
            persistPending.store(true);
          } else {
            setState(State::Error, "SIM switch was rejected");
            stopTransport();
          }
        }
      }
      if (wanted.load() && !cancel.load() && !transportStarted && snapshot().state != State::Error) {
        // Explicit 4G selection: retain saved Wi-Fi credentials, release its route.
        WiFi.mode(WIFI_OFF);
        setState(State::Starting);
        if (startTransport(UartEthModem::StartMode::kNormal, 90000)) updateInfo();
      }
      if (cancel.load()) {
        if (stopTransport()) setState(State::Off);
      }
    }
    if ((cancel.load() || !wanted.load()) && stopTransport()) setState(State::Off);
  }
  LOG_INF("CELL", "Control done state=%u enabled=%u", static_cast<unsigned>(snapshot().state), wanted.load());
  working.store(false);
  vTaskDeleteWithCaps(nullptr);
}
bool launch(Action next, uint8_t slot = 255) {
  bool idle = false;
  if (!working.compare_exchange_strong(idle, true)) return false;
  cancel.store(false);
  action = next;
  requestedSlot = slot;
  // The AT/control stack is not DMA memory; keep its 8 KiB in PSRAM. DMA and
  // UHCI interrupt metadata remain in the checked vendor internal allocations.
  if (xTaskCreatePinnedToCoreWithCaps(worker, "rick_cell_ctl", 8192, nullptr, 4, nullptr, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    working.store(false);
    setState(State::Error, "Cellular control task allocation failed");
    return false;
  }
  return true;
}
}  // namespace
void pump() {
  if (persistPending.exchange(false)) persist();
}
void begin() {
  Preferences prefs;
  bool enabled = false;
  if (prefs.begin("rickcell", true)) {
    enabled = prefs.getBool("enabled", false);
    prefs.end();
  }
  if (enabled) enable(true);
}
bool enable(bool on) {
  wanted.store(on);
  portENTER_CRITICAL(&stateMux);
  current.enabled = on;
  portEXIT_CRITICAL(&stateMux);
  persist();
  if (!on && working.load()) {
    cancel.store(true);
    return true;
  }
  if (on && transportStarted) return refresh();
  if (on) setState(State::Starting);
  return launch(on ? Action::Connect : Action::Disconnect);
}
bool selectSim(uint8_t slot) {
  if (slot > 1) return false;
  setState(State::Starting);
  return launch(Action::Switch, slot);
}
bool refresh() { return launch(Action::Refresh); }
bool probe() { return launch(Action::Probe); }
bool connected() { return snapshot().state == State::Connected; }
bool busy() { return working.load(); }
Snapshot snapshot() {
  portENTER_CRITICAL(&stateMux);
  Snapshot copy = current;
  portEXIT_CRITICAL(&stateMux);
  copy.busy = working.load();
  return copy;
}
bool shutdown() {
  if (working.load()) {
    cancel.store(true);
    const uint32_t started = millis();
    while (working.load() && millis() - started < 15000) delay(20);
    if (working.load()) return false;
  }
  return stopTransport();
}
}  // namespace HalCellular
#else
namespace HalCellular {
void begin() {}
void pump() {}
bool enable(bool) { return false; }
bool selectSim(uint8_t) { return false; }
bool refresh() { return false; }
bool probe() { return false; }
bool connected() { return false; }
bool busy() { return false; }
Snapshot snapshot() { return {}; }
bool shutdown() { return true; }
}  // namespace HalCellular
#endif
