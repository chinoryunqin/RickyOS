#pragma once

// BleInput.cpp names NimBLE-Arduino's bundled-host path for the PSRAM allocator probe;
// the Plus links IDF's own NimBLE host, whose header has the same API.
#include <esp_nimble_mem.h>
