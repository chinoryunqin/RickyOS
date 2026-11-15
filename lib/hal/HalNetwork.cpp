#include "HalNetwork.h"

#include <WiFi.h>

#include "HalCellular.h"
#include "MetalioFeatures.h"
#if RICKYOS_METALIO_CELLULAR_ENABLED
#include <esp_netif.h>

#include <atomic>
namespace {
std::atomic<bool> keepStation{false};
}
#endif

bool HalNetwork::wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool HalNetwork::wifiEnabled() { return (WiFi.getMode() & WIFI_MODE_STA) != 0; }
bool HalNetwork::wifiKept() {
#if RICKYOS_METALIO_CELLULAR_ENABLED
  return keepStation.load();
#else
  return false;
#endif
}
void HalNetwork::keepWifi() {
#if RICKYOS_METALIO_CELLULAR_ENABLED
  keepStation.store(true);
  WiFi.setAutoReconnect(true);
#endif
}
void HalNetwork::turnOffWifi() {
#if RICKYOS_METALIO_CELLULAR_ENABLED
  keepStation.store(false);
  WiFi.setAutoReconnect(false);
  WiFi.enableSTA(false);
#endif
}
void HalNetwork::releaseWifi() {
  if (!wifiKept()) WiFi.disconnect(false);
}
HalNetwork::Route HalNetwork::route() {
#if RICKYOS_METALIO_CELLULAR_ENABLED
  // ESP-NETIF selects the station (priority 100) over the modem's Ethernet
  // netif (50). Report the selected interface, rather than a radio switch.
  if (wifiConnected()) {
    const auto* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta && esp_netif_get_default_netif() == sta) return Route::Wifi;
  }
  if (HalCellular::connected()) return Route::Cellular;
#else
  if (wifiConnected()) return Route::Wifi;
#endif
  return Route::None;
}
bool HalNetwork::connected() { return route() != Route::None; }
