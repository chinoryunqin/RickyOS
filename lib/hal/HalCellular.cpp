#include "HalCellular.h"

#include "MetalioFeatures.h"
#if RICKYOS_METALIO_CELLULAR_ENABLED
#include <HalPowerManager.h>
#include "HalNetwork.h"
#include <Logging.h>
#include <Network.h>
#include <NetworkClient.h>
#include <Preferences.h>
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
bool connectPending = false;  // main loop only
bool previousWifi = false;
uint32_t retryStopAt = 0;
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
  if (state == State::Off || state == State::Starting || state == State::WifiPreferred) {
    current.carrier[0] = 0;
    current.signal = 99;
  }
  portEXIT_CRITICAL(&stateMux);
}
void onEvent(UartEthModem::UartEthModemEvent event, const std::string& detail) {
  if (cancel.load()) return;
  // Flight sessions still need to surface initialization failures with data off.
  if (!wanted.load() && event != UartEthModem::UartEthModemEvent::ErrorInitFailed) return;
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
  if (!prefs.begin("rickcell", false)) {
    LOG_ERR("CELL", "Could not open modem preferences");
    return;
  }
  const auto value = snapshot();
  if (prefs.getBool("enabled", false) != value.enabled && !prefs.putBool("enabled", value.enabled))
    LOG_ERR("CELL", "Could not save the mobile data switch");
  if (value.simSlot < 2 && prefs.getUChar("slot", 255) != value.simSlot && !prefs.putUChar("slot", value.simSlot))
    LOG_ERR("CELL", "Could not save the verified SIM slot");
  prefs.end();
}
bool stopTransport(bool enterFlight = true) {
  if (!transportStarted) return true;
  if (enterFlight) {
    std::string response;
    // SendAt also accepts commands while initialization is active.
    const auto result = modem().SendAt("AT+CFUN=4", response, 2000);
    if (result != ESP_OK) LOG_ERR("CELL", "Flight mode acknowledgement unavailable: %d", result);
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
int readSimSlot() {
  std::string response;
  if (modem().SendAt("AT+ECSIMCFG?", response, 1500) != ESP_OK) return -1;
  constexpr char field[] = "+ECSIMCFG: \"SimSlot\",";
  const auto position = response.find(field);
  if (position == std::string::npos) return -1;
  const auto value = response.find_first_not_of(" \t", position + sizeof(field) - 1);
  if (value == std::string::npos || (response[value] != '0' && response[value] != '1')) return -1;
  if (value + 1 < response.size() && strchr("\r\n \t", response[value + 1]) == nullptr) return -1;
  return response[value] - '0';
}
void updateInfo() {
  if (!transportStarted || !modem().IsInitialized() || cancel.load()) return;
  const int signal = modem().GetSignalStrength();
  if (cancel.load()) return;
  const auto carrier = modem().GetCarrierName();
  const int slot = readSimSlot();
  portENTER_CRITICAL(&stateMux);
  current.signal = signal;
  snprintf(current.carrier, sizeof(current.carrier), "%s", carrier.c_str());
  if (slot >= 0) current.simSlot = static_cast<uint8_t>(slot);
  portEXIT_CRITICAL(&stateMux);
  if (slot >= 0) persistPending.store(true);
}
bool switchSim(uint8_t slot) {
  if (!stopTransport()) return false;
  if (!startTransport(UartEthModem::StartMode::kFlight, 35000)) return false;
  if (cancel.load()) return false;
  char command[48];
  snprintf(command, sizeof(command), "AT+ECSIMCFG=\"SimSlot\",%u", slot);
  std::string response;
  if (modem().SendAt(command, response, 5000) != ESP_OK) {
    stopTransport();
    setState(State::Error, "SIM switch was rejected");
    return false;
  }
  // The module applies the selected slot on restart. Its AT link may go away
  // before the final response, so verify the setting on a fresh flight session.
  modem().SendAt("AT+CFUN=1,1", response, 3000);
  if (!stopTransport(false)) return false;
  delay(2000);
  if (cancel.load() || !startTransport(UartEthModem::StartMode::kFlight, 35000)) return false;
  const int verified = readSimSlot();
  const bool stopped = stopTransport();
  if (verified != slot) {
    setState(State::Error, "Modem did not confirm the selected SIM");
    return false;
  }
  portENTER_CRITICAL(&stateMux);
  current.simSlot = slot;
  portEXIT_CRITICAL(&stateMux);
  persistPending.store(true);
  return stopped;
}
void connectTransport() {
  if (cancel.load() || !wanted.load() || HalNetwork::wifiConnected()) return;
  if (!stopTransport()) return;
  setState(State::Starting);
  if (startTransport(UartEthModem::StartMode::kNormal, 90000)) updateInfo();
}
void worker(void*) {
  const Action job = action;
  const uint8_t slot = requestedSlot;
  {
    HalPowerManager::Lock power;
    if (job == Action::Probe) {
      NetworkClient client;
      client.setTimeout(5000);
      const bool tcp = !HalNetwork::wifiConnected() && connected() && client.connect("example.com", 80, 5000);
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
    } else if (job == Action::Switch) {
      if (switchSim(slot)) setState(State::Off);
    } else if (job == Action::Connect) {
      connectTransport();
    }

    const bool wifiPreferred = wanted.load() && HalNetwork::wifiConnected();
    if (job == Action::Disconnect || cancel.load() || !wanted.load() || wifiPreferred) {
      if (stopTransport() && (snapshot().state != State::Error || cancel.load() || job == Action::Disconnect)) {
        setState(wifiPreferred ? State::WifiPreferred : State::Off);
      }
    }
    if (job == Action::Switch) {
      portENTER_CRITICAL(&stateMux);
      current.pendingSimSlot = 255;
      portEXIT_CRITICAL(&stateMux);
    }
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
  const bool wifi = HalNetwork::wifiConnected();
  const auto state = snapshot();
  if (previousWifi && !wifi && state.enabled) connectPending = true;
  previousWifi = wifi;

  if (state.busy) {
    // Switching SIM uses a temporary flight-mode transport. Allow it while
    // on Wi-Fi, but stop a dial in progress as soon as Wi-Fi has an IP.
    if (wifi && action == Action::Connect) cancel.store(true);
    return;
  }

  if (wifi || !state.enabled) {
    if (transportStarted) {
      const uint32_t now = millis();
      if (static_cast<int32_t>(now - retryStopAt) >= 0) {
        retryStopAt = now + 3000;
        launch(Action::Disconnect);
      }
    } else if (state.state != State::Error || connectPending) {
      const auto preferred = state.enabled ? State::WifiPreferred : State::Off;
      if (state.state != preferred) setState(preferred);
    }
    return;
  }

  if (state.state == State::WifiPreferred || (!transportStarted && state.state == State::Off) || connectPending) {
    connectPending = false;
    launch(Action::Connect);
  }
}
void begin() {
  Preferences prefs;
  if (prefs.begin("rickcell", true)) {
    current.enabled = prefs.getBool("enabled", false);
    current.simSlot = prefs.getUChar("slot", 255);
    if (current.simSlot > 1) current.simSlot = 255;
    prefs.end();
  }
  wanted.store(current.enabled);
}
bool enable(bool on) {
  wanted.store(on);
  portENTER_CRITICAL(&stateMux);
  const bool changed = current.enabled != on;
  current.enabled = on;
  portEXIT_CRITICAL(&stateMux);
  if (changed) persistPending.store(true);
  if (!on) {
    connectPending = false;
    if (!working.load() && !transportStarted) setState(State::Off);
    if (working.load() && action != Action::Switch) cancel.store(true);
  } else {
    connectPending = changed || snapshot().state == State::Error;
  }
  retryStopAt = millis();
  pump();
  return true;
}
bool selectSim(uint8_t slot) {
  if (slot > 1 || working.load()) return false;
  connectPending = false;
  portENTER_CRITICAL(&stateMux);
  current.pendingSimSlot = slot;
  portEXIT_CRITICAL(&stateMux);
  setState(State::Starting);
  if (launch(Action::Switch, slot)) return true;
  portENTER_CRITICAL(&stateMux);
  current.pendingSimSlot = 255;
  portEXIT_CRITICAL(&stateMux);
  return false;
}
bool refresh() {
  if (working.load() || HalNetwork::wifiConnected() || !wanted.load()) return false;
  if (!transportStarted || !connected()) {
    connectPending = true;
    pump();
    return true;
  }
  return launch(Action::Refresh);
}
bool probe() { return !HalNetwork::wifiConnected() && connected() && launch(Action::Probe); }
bool connected() {
  const auto state = snapshot();
  if (!state.enabled || state.state != State::Connected) return false;
  const auto* cell = esp_netif_get_handle_from_ifkey("ETH_DEF");
  return cell && esp_netif_get_default_netif() == cell;
}
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
  if (!stopTransport()) return false;
  setState(State::Off);
  return true;
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
