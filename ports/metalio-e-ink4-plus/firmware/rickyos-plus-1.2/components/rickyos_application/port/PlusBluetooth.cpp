// Bluetooth page turners on the Plus: the S31 BLE controller with IDF's NimBLE host.
#include <sdkconfig.h>

#if CONFIG_BT_NIMBLE_ENABLED
#include <esp32-hal-bt.h>

// initArduino() releases the BLE controller memory unless a BLE library claims it.
// NimBLE-Arduino is compiled here in its IDF mode, which does not set that claim, so
// keep the memory for the page-turner host and runtime control-center switching.
// NetworkStartup stops active BLE for Wi-Fi without irreversibly releasing this
// reservation on Plus. Classic Bluetooth memory is released as before.
extern "C" __attribute__((used)) bool bleInUse(void) { return true; }

#if !CONFIG_BT_NIMBLE_GAP_SERVICE
// A central-only host has no GATT server, so IDF builds no GAP service; NimBLE-Arduino
// still names the device through it at init. The name is never shown to a peer.
extern "C" int ble_svc_gap_device_name_set(const char*) { return 0; }
#endif
#endif
