#pragma once
#include <cstddef>
#include <cstdint>

namespace HalCamera {
enum class State : uint8_t { Idle, Working, Saved, Error, Preview };
struct Snapshot {
  State state = State::Idle;
  uint32_t bytes = 0, generation = 0;
  bool frameReady = false;
  uint16_t width = 0, height = 0;
  int error = 0;
  uint32_t configDescriptorBytes = 0;
  uint8_t powerBefore[5]{}, powerAfter[5]{};
  char usbError[256]{};
  uint32_t hostPort = 0, hostInterrupt = 0, phyRoute = 0, usbSerial = 0;
  uint16_t muxOutput = 0;
  uint8_t powerLate[5]{};
  uint32_t phase = 0, internalBefore = 0, internalAfter = 0, largestBefore = 0, largestAfter = 0;
  char path[160]{};
};
void recoverUsbBeforeSerial();
void begin(const char* photoDirectory);
void pump();  // Main/internal-stack task only: commits pending NVS settings.
bool capture();
bool requestPreview();
bool saveCurrent();
struct Frame {
  const uint8_t* data = nullptr;
  size_t size = 0;
  uint16_t width = 0, height = 0;
};
// Borrowed until the next accepted request. Decode only while !busy().
Frame frame();
bool busy();
bool usbOwned();
Snapshot snapshot();
// Call from the main loop after leaving the camera, outside any render lock.
void leave();
bool consumeRestartRequest();
bool prepareForSleep();
}  // namespace HalCamera
