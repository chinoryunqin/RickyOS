#pragma once
#include <functional>
#include <utility>

class OptionPopup {
 public:
  inline static OptionPopup* shown = nullptr;
  bool active = false;
  int selected = -1;
  std::function<void(int)> callback;
  void showMessage(const char*, const char*, const char* const*, int count, int current,
                   std::function<void(int)> handler) {
    if (count != 2) return;
    active = true;
    selected = current;
    callback = std::move(handler);
    shown = this;
  }
  bool isActive() const { return active; }
  template <typename Input, typename Update>
  bool handleInput(Input&, Update) {
    return active;
  }
  template <typename Renderer, typename Input>
  bool processRender(Renderer&, const Input&) {
    return active;
  }
  void dismiss() {
    active = false;
    callback = nullptr;
    if (shown == this) shown = nullptr;
  }
  static void choose(int value) {
    auto* popup = shown;
    if (!popup) return;
    popup->active = false;
    shown = nullptr;
    if (popup->callback) popup->callback(value);
  }
};
