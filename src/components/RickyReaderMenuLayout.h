#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Maps presentation slots to the existing reader action table. No action IDs,
// book state, or callback contracts change. Both views fit in kMaxItems
// ListItems; the only extra residency is this bounded index map.
namespace RickyReaderMenuLayout {
inline constexpr int8_t More = -1;
// Every action the reader can list (18 with reading mode and lines) plus the More row.
inline constexpr size_t kMaxItems = 20;
struct View {
  std::array<int8_t, kMaxItems> indices{};
  uint8_t count = 0;
  uint8_t quickCount = 0;
};

template <typename Action, typename Items>
View build(const Items& items, bool more) {
  View view;
  const auto add = [&](Action action) {
    // Search every item: the index map bounds the view, not where an action sits.
    for (size_t i = 0; i < items.size() && i < static_cast<size_t>(INT8_MAX); ++i) {
      if (items[i].action == action && view.count < view.indices.size()) {
        view.indices[view.count++] = static_cast<int8_t>(i);
        return;
      }
    }
  };
  if (more) {
    for (auto action : {Action::FOOTNOTES, Action::AUTO_PAGE_TURN, Action::SYNC, Action::SCREENSHOT, Action::DISPLAY_QR,
                        Action::GO_HOME, Action::DELETE_CACHE})
      add(action);
    return view;
  }
  for (auto action : {Action::SELECT_CHAPTER, Action::TOGGLE_BOOKMARK, Action::TEXT_SETTINGS, Action::DICTIONARY})
    add(action);
  view.quickCount = view.count;
  // Reading mode and reading lines join the first page; sync and auto turn move to More
  // so the page still fits one screen.
  for (auto action : {Action::NIGHT_MODE, Action::FRONTLIGHT, Action::READING_MODE, Action::READING_LINE,
                      Action::ROTATE_SCREEN, Action::GO_TO_PERCENT, Action::BOOKMARKS})
    add(action);
  if (view.count < view.indices.size()) view.indices[view.count++] = More;
  return view;
}
}  // namespace RickyReaderMenuLayout
