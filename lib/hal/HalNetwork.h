#pragma once
#include <cstdint>

namespace HalNetwork {
enum class Route : uint8_t { None, Wifi, Cellular };
Route route();
bool connected();
bool wifiConnected();
bool wifiEnabled();
// A station selected by the user is shared across app network sessions.
bool wifiKept();
void keepWifi();
void turnOffWifi();
void releaseWifi();  // Disconnect only an app-owned station.
}  // namespace HalNetwork
