#pragma once

#include <WiFi.h>

class GfxRenderer;

namespace NetworkStartup {

// One snapshot per network lifecycle transition; no persistent buffers.
void logMemory(const char* stage);

// Preserve render memory when PSRAM/internal SRAM headroom is healthy; otherwise
// release it before the Wi-Fi driver starts allocating.
void prepare(GfxRenderer& renderer);

// Append one line (version, uptime, stage, code, heap) to /.crosspoint/wifi-errors.txt so a
// failure seen away from a computer still leaves a record. Bounded to a few KB.
void recordFailure(const char* stage, int code);

// The only entry point for enabling STA/AP mode in application code.
bool setMode(GfxRenderer& renderer, wifi_mode_t mode);

}  // namespace NetworkStartup
