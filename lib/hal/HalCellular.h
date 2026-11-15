#pragma once
#include <cstdint>

namespace HalCellular {
enum class State : uint8_t { Off, Starting, Connected, NoSim, Denied, Searching, Error, WifiPreferred };
struct Snapshot {
  State state = State::Off;
  bool enabled = false;
  bool busy = false;
  uint8_t simSlot = 255;
  uint8_t pendingSimSlot = 255;
  int signal = 99;
  char carrier[48]{};
  char ip[40]{};
  char error[96]{};
  bool operator==(const Snapshot&) const = default;
};
void begin();
void pump();
bool enable(bool on);
bool selectSim(uint8_t slot);
bool refresh();
bool probe();
bool connected();
bool busy();
Snapshot snapshot();
// Join the control job and modem tasks before board power/storage shutdown.
bool shutdown();
}  // namespace HalCellular
