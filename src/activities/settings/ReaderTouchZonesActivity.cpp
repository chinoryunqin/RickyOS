#include "ReaderTouchZonesActivity.h"
#include <algorithm>
#include <cstdio>
#include "CrossPointSettings.h"
#include "components/RickyDesignUi.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace {
constexpr StrId kActions[] = {StrId::STR_TOUCH_GRID_IGNORE, StrId::STR_TOUCH_GRID_PREVIOUS,
  StrId::STR_TOUCH_GRID_NEXT, StrId::STR_TOUCH_GRID_MENU, StrId::STR_TOUCH_GRID_REFRESH, StrId::STR_TOUCH_GRID_CONTENTS, StrId::STR_KEY_ADD_BOOKMARK};
static_assert(std::size(kActions) == readerTapZones::value(readerTapZones::Action::Count));
freeink::ui::Rect ui(Rect r) {
  return {static_cast<int16_t>(r.x), static_cast<int16_t>(r.y), static_cast<int16_t>(r.width), static_cast<int16_t>(r.height)};
}
bool inside(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height; }
}
void ReaderTouchZonesActivity::onEnter() {
  Activity::onEnter();
  savedOrientation_ = renderer.getOrientation();
  bank_ = readerTapZones::bank(renderer.getScreenWidth(), renderer.getScreenHeight());
  waitConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  const auto snapshot = touchSnapshotFrom(mappedInput);
  waitTouchRelease_ = snapshot.touchPressed || snapshot.touchHeld || snapshot.touchReleased;
  requestUpdate();
}
void ReaderTouchZonesActivity::onExit() {
  renderer.setOrientation(savedOrientation_);
  Activity::onExit();
}
ReaderTouchZonesActivity::Layout ReaderTouchZonesActivity::layout() const {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pad = std::max(16, safe.width / 18), gap = 12;
  const int x = safe.x + pad, w = safe.width - pad * 2;
  const int small = renderer.getLineHeight(RickyDesignUi::CAPTION);
  const int row = std::max(48, small + 24);
  int y = safe.y + metrics.topPadding;
  Layout l;
  l.header = Rect{safe.x, y, safe.width, metrics.headerHeight}; y += metrics.headerHeight + gap * 2;
  if (choosing_) {
    l.enabled = Rect{x, y, w, row}; y += row + gap * 3;
    const int choiceHeight = small * 2 + 40;
    for (int b = 0; b < 2; ++b) {
      l.choices[b] = Rect{x, y, w, choiceHeight}; y += choiceHeight + gap * 2;
    }
    l.note = Rect{x, y, w, small * 2};
    return l;
  }
  // These are the exact logical screen partitions used by cellAt(), not
  // inset controls. The two floating tools overlay them without resizing them.
  const int sw = renderer.getScreenWidth(), sh = renderer.getScreenHeight();
  l.grid = Rect{0, 0, sw, sh};
  for (int i = 0; i < 9; ++i) {
    const int col = i % 3, r = i / 3;
    const int left = (col * sw + 2) / 3, right = ((col + 1) * sw + 2) / 3;
    const int top = (r * sh + 2) / 3, bottom = ((r + 1) * sh + 2) / 3;
    l.cells[i] = Rect{left, top, right - left, bottom - top};
  }
  l.hint = Rect{pad, pad, sw - pad * 2, small * 2};
  const int toolY = sh - std::min(pad, sh / 24) - row;
  const int resetWidth = std::min(sw / 2 - gap, small * 10);
  const int doneWidth = std::min(sw / 3, small * 5);
  l.reset = Rect{pad, toolY, resetWidth, row};
  l.done = Rect{sw - pad - doneWidth, toolY, doneWidth, row};
  l.note = Rect{pad, l.cells[0].height - small * 2 - gap, sw - pad * 2, small * 2};
  return l;
}
void ReaderTouchZonesActivity::save() {
  readerTapZones::normalize(SETTINGS.readerTapZones);
  SETTINGS.saveToFile();
}
void ReaderTouchZonesActivity::navigateBack() {
  if (choosing_) { finish(); return; }
  renderer.setOrientation(savedOrientation_);
  choosing_ = true; selected_ = bank_ + 1; warning_ = false;
  requestUpdate();
}
void ReaderTouchZonesActivity::activate(int index) {
  selected_ = index; warning_ = false;
  if (choosing_) {
    if (index == 0) {
      SETTINGS.readerTapZones.enabled = !SETTINGS.readerTapZones.enabled;
      if (SETTINGS.readerTapZones.enabled) SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_ON;
      save();
    } else if (index == 1 || index == 2) {
      bank_ = index - 1; choosing_ = false; selected_ = 5;
      // Editing is at the actual reading resolution and orientation. The
      // caller's orientation is restored on Back, Done and activity exit.
      renderer.setOrientation(bank_ == 0 ? GfxRenderer::Orientation::Portrait :
          GfxRenderer::Orientation::LandscapeClockwise);
      // The selection release belongs to the parent page, not the first cell.
      waitConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
      const auto snapshot = touchSnapshotFrom(mappedInput);
      waitTouchRelease_ = snapshot.touchPressed || snapshot.touchHeld || snapshot.touchReleased;
    }
    requestUpdate(); return;
  }
  if (index >= 1 && index <= 9) {
    const int cell = index - 1;
    const int current = SETTINGS.readerTapZones.layouts[bank_][cell];
    popup_.show(StrId::STR_TOUCH_GRID_ACTION, kActions, static_cast<int>(std::size(kActions)), current,
      [this, cell](int choice) {
        RenderLock lock(*this);
        auto next = SETTINGS.readerTapZones.layouts[bank_];
        next[cell] = static_cast<uint8_t>(choice);
        if (!readerTapZones::hasMenu(next)) { warning_ = true; return; }
        SETTINGS.readerTapZones.layouts[bank_] = next;
        save();
      });
  } else if (index == 11) {
    navigateBack(); return;
  } else if (index == 10) {
    SETTINGS.readerTapZones.layouts[bank_] = readerTapZones::standard();
    save();
  }
  requestUpdate();
}
void ReaderTouchZonesActivity::loop() {
  if (popup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  RenderLock lock(*this);
  if (waitConfirmRelease_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitConfirmRelease_ = false;
    return;
  }
  if (waitTouchRelease_) {
    const auto snapshot = touchSnapshotFrom(mappedInput);
    if (!snapshot.touchPressed && !snapshot.touchHeld) waitTouchRelease_ = false;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { navigateBack(); return; }
  const int first = choosing_ ? 0 : 1, count = choosing_ ? 3 : 11;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selected_ = first + (selected_ - first + count - 1) % count; requestUpdate(); return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selected_ = first + (selected_ - first + 1) % count; requestUpdate(); return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) { activate(selected_); return; }
  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  const auto l = layout();
  if (choosing_ && inside(l.header, x, y) && x < l.header.x + l.header.width / 5) { navigateBack(); return; }
  if (choosing_) {
    if (inside(l.enabled, x, y)) { activate(0); return; }
    for (int b = 0; b < 2; ++b) if (inside(l.choices[b], x, y)) { activate(b + 1); return; }
    return;
  }
  // Floating tools win over the underlying bottom cells; every other tap
  // uses the reader's production mapping, including the exact edge pixels.
  if (inside(l.done, x, y)) { navigateBack(); return; }
  if (inside(l.reset, x, y)) { activate(10); return; }
  const int cell = readerTapZones::cellAt(x, y, renderer.getScreenWidth(), renderer.getScreenHeight());
  if (cell >= 0) activate(cell + 1);
}
void ReaderTouchZonesActivity::render(RenderLock&&) {
  const auto l = layout();
  renderer.clearScreen();
  if (choosing_) GUI.drawHeader(renderer, l.header, tr(STR_TOUCH_GRID_TITLE));
  namespace ds = ricky::ds;
  if (choosing_) {
    const bool enabled = SETTINGS.readerTapZones.enabled;
    auto label = ui(l.enabled); label.width -= 90;
    RickyDesignUi::text(renderer, label, tr(STR_TOUCH_GRID_ENABLE), RickyDesignUi::BODY);
    // Reuse the product's small switch language, not a screen-wide dark button.
    const int switchWidth = 64, switchHeight = 34;
    const int sx = l.enabled.x + l.enabled.width - switchWidth;
    const int sy = l.enabled.y + (l.enabled.height - switchHeight) / 2;
    renderer.drawRoundedRect(sx, sy, switchWidth, switchHeight, 2, switchHeight / 2, true);
    RickyDesignUi::fill(renderer, {static_cast<int16_t>(sx + (enabled ? 35 : 5)),
        static_cast<int16_t>(sy + 5), 24, 24}, ds::gray::kInk, 12);
    if (selected_ == 0) RickyDesignUi::fill(renderer, {static_cast<int16_t>(l.enabled.x - 10),
        static_cast<int16_t>(l.enabled.y + l.enabled.height / 2 - 3), 4, 6}, ds::gray::kInk);
    for (int b = 0; b < 2; ++b) {
      const auto r = ui(l.choices[b]);
      auto label = r; label.x += 52; label.width -= 84;
      RickyDesignUi::text(renderer, label, b == 0 ? tr(STR_TOUCH_GRID_PORTRAIT_PAGE) :
          tr(STR_TOUCH_GRID_LANDSCAPE_PAGE), RickyDesignUi::BODY);
      const int iw = b == 0 ? 20 : 30, ih = b == 0 ? 30 : 20;
      renderer.drawRoundedRect(r.x + 4, r.y + (r.height - ih) / 2, iw, ih, 2, 3, true);
      renderer.drawLine(r.right() - 14, r.y + r.height / 2 - 6, r.right() - 8, r.y + r.height / 2, true);
      renderer.drawLine(r.right() - 8, r.y + r.height / 2, r.right() - 14, r.y + r.height / 2 + 6, true);
      RickyDesignUi::rule(renderer, {r.x, static_cast<int16_t>(r.bottom() - 1), r.width, 1});
      if (selected_ == b + 1) RickyDesignUi::fill(renderer,
          {static_cast<int16_t>(r.x - 10), static_cast<int16_t>(r.y + r.height / 2 - 3), 4, 6}, ds::gray::kInk);
    }
    RickyDesignUi::secondaryText(renderer, ui(l.note), tr(STR_TOUCH_GRID_NOTE), RickyDesignUi::CAPTION,
        false, freeink::ui::TextAlign::Left, 2);
    renderer.displayBuffer(); return;
  }
  // Full-screen setup canvas: fill and labels stay inside each actual zone.
  for (int i = 0; i < 9; ++i) {
    const auto r = ui(l.cells[i]);
    if (selected_ == i + 1) RickyDesignUi::fill(renderer, r, ds::gray::kTint);
    const auto a = SETTINGS.readerTapZones.layouts[bank_][i];
    RickyDesignUi::text(renderer, r, I18N.get(kActions[a < std::size(kActions) ? a : 0]),
        RickyDesignUi::BODY, false, freeink::ui::TextAlign::Center, 2);
  }
  for (int i = 1; i < 3; ++i) {
    const int vx = l.cells[i].x, hy = l.cells[i * 3].y;
    RickyDesignUi::fill(renderer, {static_cast<int16_t>(vx), 0, 1, static_cast<int16_t>(l.grid.height)}, ds::gray::kLine);
    RickyDesignUi::fill(renderer, {0, static_cast<int16_t>(hy), static_cast<int16_t>(l.grid.width), 1}, ds::gray::kLine);
  }
  RickyDesignUi::text(renderer, ui(l.hint), bank_ == 0 ? tr(STR_TOUCH_GRID_PORTRAIT_PAGE) :
      tr(STR_TOUCH_GRID_LANDSCAPE_PAGE), RickyDesignUi::CAPTION, false, freeink::ui::TextAlign::Center);
  const Rect tools[] = {l.reset, l.done};
  for (int i = 0; i < 2; ++i) {
    auto r = ui(tools[i]);
    RickyDesignUi::fill(renderer, r, ds::gray::kPaper, 6);
    renderer.drawRoundedRect(r.x, r.y, r.width, r.height, 1, 6, true);
    RickyDesignUi::text(renderer, r, i == 0 ? tr(STR_TOUCH_GRID_RESET) : tr(STR_DONE),
        RickyDesignUi::CAPTION, false, freeink::ui::TextAlign::Center);
    if (selected_ == i + 10) RickyDesignUi::fill(renderer,
        {static_cast<int16_t>(r.x + 12), static_cast<int16_t>(r.bottom() - 4), static_cast<int16_t>(r.width - 24), 2}, ds::gray::kInk);
  }
  RickyDesignUi::secondaryText(renderer, ui(l.note), warning_ ? tr(STR_TOUCH_GRID_KEEP_MENU) : tr(STR_TOUCH_GRID_HINT),
      RickyDesignUi::CAPTION, false, freeink::ui::TextAlign::Center, 2);
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}
