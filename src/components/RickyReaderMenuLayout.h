#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Maps presentation slots to the existing reader action table. No action IDs,
// book state, or callback contracts change. Both views fit in the original
// sixteen ListItems; the only extra residency is this bounded index map.
namespace RickyReaderMenuLayout {
inline constexpr int8_t More = -1;
struct View {
  std::array<int8_t, 16> indices{};
  uint8_t count = 0;
  uint8_t quickCount = 0;
};

template <typename Action, typename Items>
View build(const Items& items, bool more) {
  View view;
  const auto add = [&](Action action) {
    for (size_t i = 0; i < items.size() && i < view.indices.size(); ++i) {
      if (items[i].action == action && view.count < view.indices.size()) {
        view.indices[view.count++] = static_cast<int8_t>(i);
        return;
      }
    }
  };
  if (more) {
    for (auto action :
         {Action::FOOTNOTES, Action::SCREENSHOT, Action::DISPLAY_QR, Action::GO_HOME, Action::DELETE_CACHE})
      add(action);
    return view;
  }
  for (auto action : {Action::SELECT_CHAPTER, Action::TOGGLE_BOOKMARK, Action::TEXT_SETTINGS, Action::DICTIONARY})
    add(action);
  view.quickCount = view.count;
  for (auto action : {Action::NIGHT_MODE, Action::FRONTLIGHT, Action::ROTATE_SCREEN, Action::AUTO_PAGE_TURN,
                      Action::GO_TO_PERCENT, Action::BOOKMARKS, Action::SYNC})
    add(action);
  if (view.count < view.indices.size()) view.indices[view.count++] = More;
  return view;
}
}  // namespace RickyReaderMenuLayout
