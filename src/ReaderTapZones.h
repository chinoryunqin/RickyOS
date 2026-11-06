#pragma once
#include <array>
#include <cstdint>

// Logical screen coordinates: rotation is handled by MappedInputManager before
// this mapping. Explicit assignments never change with hand/RTL preferences.
namespace readerTapZones {
enum class Action : uint8_t { Ignore, Previous, Next, Menu, Refresh, Contents, AddBookmark, Count };
using Layout = std::array<uint8_t, 9>;
constexpr uint8_t value(Action a) { return static_cast<uint8_t>(a); }
constexpr Layout standard() {
  return {value(Action::Previous), value(Action::Menu), value(Action::Next),
          value(Action::Previous), value(Action::Menu), value(Action::Next),
          value(Action::Previous), value(Action::Next), value(Action::Next)};
}
struct Config {
  bool enabled = false;
  std::array<Layout, 2> layouts{standard(), standard()};
};
constexpr int bank(int width, int height) { return width > height ? 1 : 0; }
constexpr int cellAt(int x, int y, int width, int height) {
  if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height) return -1;
  return static_cast<int>(static_cast<int64_t>(y) * 3 / height) * 3 +
         static_cast<int>(static_cast<int64_t>(x) * 3 / width);
}
constexpr bool hasMenu(const Layout& layout) {
  for (auto a : layout) if (a == value(Action::Menu)) return true;
  return false;
}
inline void normalize(Config& config) {
  for (auto& layout : config.layouts) {
    for (auto& a : layout) if (a >= value(Action::Count)) a = value(Action::Ignore);
    // Recover safely from edited/broken settings; the device menu must stay reachable.
    if (!hasMenu(layout)) layout[4] = value(Action::Menu);
  }
}
inline Action actionAt(const Config& config, int x, int y, int width, int height) {
  const int cell = cellAt(x, y, width, height);
  if (!config.enabled || cell < 0) return Action::Ignore;
  const auto action = config.layouts[bank(width, height)][cell];
  return action < value(Action::Count) ? static_cast<Action>(action) : Action::Ignore;
}
}  // namespace readerTapZones
