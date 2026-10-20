#include "SkillGomokuActivity.h"

#include <Arduino.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "GomokuLayout.h"
#include "GomokuModeActivity.h"
#include "activities/apps/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;
using skillgomoku::Action;
using skillgomoku::Game;
using skillgomoku::Skill;

namespace {
constexpr fui::ActionId Control = 1;
constexpr fui::ActionId Board = 2;
const char* skillName(Skill skill) {
  switch (skill) {
    case Skill::Remove:
      return tr(STR_SGM_REMOVE);
    case Skill::Convert:
      return tr(STR_SGM_CONVERT);
    case Skill::Move:
      return tr(STR_SGM_MOVE);
    case Skill::Freeze:
      return tr(STR_SGM_FREEZE);
    case Skill::Reverse:
      return tr(STR_SGM_REVERSE);
    case Skill::Wipe:
      return tr(STR_SGM_WIPE);
    case Skill::Rest:
      return tr(STR_SGM_REST);
    default:
      return tr(STR_GOMOKU_PLACE);
  }
}
const char* skillHint(Skill skill, bool pickingSource) {
  switch (skill) {
    case Skill::Remove:
      return tr(STR_SGM_REMOVE_HINT);
    case Skill::Convert:
      return tr(STR_SGM_CONVERT_HINT);
    case Skill::Move:
      return pickingSource ? tr(STR_SGM_MOVE_FROM) : tr(STR_SGM_MOVE_TO);
    case Skill::Freeze:
      return tr(STR_SGM_FREEZE_HINT);
    case Skill::Reverse:
      return tr(STR_SGM_REVERSE_HINT);
    case Skill::Wipe:
      return tr(STR_SGM_WIPE_HINT);
    case Skill::Rest:
      return tr(STR_SGM_REST_HINT);
    default:
      return tr(STR_SGM_PLACE_HINT);
  }
}
void text(fui::DrawTarget& target, fui::Rect rect, const char* label, fui::TextStyle style, int lines = 1) {
  style.maxLines = lines;
  target.text(rect, label, style);
}
}  // namespace

SkillGomokuActivity::SkillGomokuActivity(GfxRenderer& renderer, MappedInputManager& input)
    : Activity("SkillGomoku", renderer, input), UiAppHost(renderer) {}

void SkillGomokuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  resetUi();
  app.on(Control, &SkillGomokuActivity::onControl, this);
  app.setScreen(&SkillGomokuActivity::screenFn, this);
  hasSave = store.load(game, versusComputer, cursor);
  requestUpdate();
}

void SkillGomokuActivity::onExit() {
  closeRouting();
  flushSave();
  Activity::onExit();
}

void SkillGomokuActivity::prepareForSleep() {
  // ActivityManager::prepareForSleep() already owns the render lock.
  flushSave();
}

void SkillGomokuActivity::flushSave() {
  if (!dirty) return;
  saveFailed = !store.save(game, versusComputer, cursor);
  if (!saveFailed) {
    dirty = false;
    hasSave = true;
  } else {
    saveDue = millis() + 5000;
  }
}

void SkillGomokuActivity::changePage(Page next) {
  app.clearTapFlash();
  closeRouting();
  page = next;
  selected = 0;
  releaseBarrier = true;
  requestUpdate();
}

int SkillGomokuActivity::itemCount() const {
  if (page == Page::Menu || page == Page::Pause) return 6;
  if (page == Page::Skills) return 8;
  return 2;
}

void SkillGomokuActivity::start(uint8_t size, bool ai) {
  game.reset(size);
  versusComputer = ai;
  cursor = (size * size) / 2;
  historyCount = 0;
  hasSave = true;  // this Activity can resume its in-memory game even if the SD save fails
  armed = Skill::Place;
  source = Game::None;
  notice = nullptr;
  thinking = false;
  dirty = true;
  saveDue = millis() + 1500;
  changePage(Page::Board);
}

void SkillGomokuActivity::commit(Action action) {
  if (!game.legal(action)) {
    notice = tr(STR_SGM_INVALID);
    requestUpdate();
    return;
  }
  if (historyCount == 8) {
    for (int i = 1; i < 8; ++i) history[i - 1] = history[i];
    --historyCount;
  }
  history[historyCount++] = game;
  game.apply(action);
  notice = action.skill == Skill::Freeze ? tr(STR_SGM_SKIPPED) : nullptr;
  if (action.skill != Skill::Place && action.skill != Skill::Freeze && action.skill != Skill::Resign) {
    snprintf(actionNotice, sizeof(actionNotice), "%s · %s",
             game.lastActor == 1 ? tr(STR_GOMOKU_BLACK) : tr(STR_GOMOKU_WHITE), skillName(action.skill));
    notice = actionNotice;
  }
  armed = Skill::Place;
  source = Game::None;
  dirty = true;
  saveDue = millis() + 1500;
  requestUpdate();
}

void SkillGomokuActivity::confirm() {
  if (thinking || (versusComputer && game.turn == 2 && !game.winner)) return;
  if (game.winner) {
    changePage(Page::Pause);
    return;
  }
  if (armed == Skill::Move && source == Game::None) {
    if (game.cells[cursor] != game.turn)
      notice = tr(STR_SGM_MOVE_FROM);
    else {
      source = cursor;
      notice = nullptr;
    }
    requestUpdate();
    return;
  }
  commit({armed, cursor, source});
}

void SkillGomokuActivity::undo() {
  int index = historyCount - 1;
  if (versusComputer)
    while (index >= 0 && history[index].turn != 1) --index;
  if (index < 0) {
    notice = tr(STR_SGM_NO_UNDO);
    return;
  }
  game = history[index];
  historyCount = index;
  thinking = false;
  armed = Skill::Place;
  source = Game::None;
  notice = nullptr;
  dirty = true;
  saveDue = millis() + 1500;
}

void SkillGomokuActivity::activate(int index) {
  if (index < 0 || index >= itemCount()) return;
  if (page == Page::Menu) {
    if (index == 0 && hasSave) {
      armed = Skill::Place;
      source = Game::None;
      changePage(Page::Board);
    } else if (index >= 1 && index <= 4) {
      start(index <= 2 ? 15 : 9, index == 1 || index == 3);
    } else if (index == 5) {
      rulesReturn = Page::Menu;
      rulesPage = 0;
      changePage(Page::Rules);
    }
  } else if (page == Page::Pause) {
    if (index == 0) changePage(Page::Board);
    if (index == 1 && !game.winner && !(versusComputer && game.turn == 2)) changePage(Page::Skills);
    if (index == 2) {
      undo();
      changePage(Page::Board);
    }
    if (index == 3) {
      rulesReturn = Page::Pause;
      rulesPage = 0;
      changePage(Page::Rules);
    }
    if (index == 4 && !game.winner && !(versusComputer && game.turn == 2)) {
      thinking = false;
      commit({Skill::Resign});
      changePage(Page::Board);
    }
    if (index == 5) {
      flushSave();
      changePage(Page::Menu);
    }
  } else if (page == Page::Skills) {
    if (index == 7) {
      armed = Skill::Place;
      source = Game::None;
      changePage(Page::Board);
    } else {
      const auto skill = static_cast<Skill>(index + 1);
      if (!game.available(skill)) return;
      armed = skill;
      source = Game::None;
      notice = nullptr;
      changePage(Page::Board);
    }
  } else if (page == Page::Rules) {
    if (index == 0)
      back();
    else {
      rulesPage = (rulesPage + 1) % 3;
      requestUpdate();
    }
  } else if (page == Page::Board) {
    if (index == 0) confirm();
    if (index == 1) {
      if (game.winner)
        changePage(Page::Pause);
      else if (!thinking && !(versusComputer && game.turn == 2))
        changePage(Page::Skills);
    }
  }
}

void SkillGomokuActivity::back() {
  if (page == Page::Menu) {
    app.clearTapFlash();
    activityManager.replaceActivityWith<GomokuModeActivity>();
  } else if (page == Page::Rules)
    changePage(rulesReturn);
  else if (page == Page::Board && armed != Skill::Place) {
    armed = Skill::Place;
    source = Game::None;
    notice = nullptr;
    requestUpdate();
  } else if (page == Page::Board)
    changePage(Page::Pause);
  else
    changePage(Page::Board);
}

void SkillGomokuActivity::loop() {
  // One lock covers the FUI interaction generation and every state mutation.
  // requestUpdate() is deferred; never wait for render while holding this lock.
  RenderLock lock(*this);
  if (dirty && static_cast<int32_t>(millis() - saveDue) >= 0) flushSave();
  if (releaseBarrier) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.isPressed(MappedInputManager::Button::Back))
      releaseBarrier = false;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    back();
    return;
  }
  if (page == Page::Board && versusComputer && game.turn == 2 && !game.winner) {
    if (!thinking) {
      computer.begin(game);
      thinking = true;
      requestUpdate();
    } else if (computer.step(game, scratch)) {
      thinking = false;
      const auto action = computer.result();
      cursor = action.target < game.size * game.size ? action.target : cursor;
      commit(action);
    }
    return;
  }
  const auto routed = routeTouch(mappedInput);
  if (routed && routed.event.action == Board && page == Page::Board && !game.winner) {
    int row = 0, column = 0;
    if (gameIntersectionFromPoint(boardX, boardY, pitch, game.size, game.size, routed.snap.touchX, routed.snap.touchY,
                                  row, column)) {
      cursor = row * game.size + column;
      notice = nullptr;
      app.clearTapFlash();
      requestUpdate();
    }
    return;
  }
  if (routed) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (page == Page::Board)
      confirm();
    else
      activate(selected);
    return;
  }
  int delta = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) delta = -1;
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) delta = 1;
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) delta = page == Page::Board ? -game.size : -1;
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) delta = page == Page::Board ? game.size : 1;
  if (!delta) return;
  if (page == Page::Board)
    cursor = (cursor + delta + game.size * game.size) % (game.size * game.size);
  else
    selected = (selected + delta + itemCount()) % itemCount();
  notice = nullptr;
  requestUpdate();
}

void SkillGomokuActivity::onControl(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<SkillGomokuActivity*>(user);
  if (event.value == 8)
    self->back();
  else
    self->activate(event.value);
}

void SkillGomokuActivity::screenFn(UiScreen& screen, void* user) {
  static_cast<SkillGomokuActivity*>(user)->buildScreen(screen);
}

void SkillGomokuActivity::buildScreen(UiScreen& screen) {
  const auto& m = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int line = screen.target().lineHeight(screen.theme().bodyText.font);
  const int gap = std::max(8, int(screen.theme().spaceMd));
  const int side = std::max(16, int(screen.theme().spaceLg));
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + m.topPadding),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width + side),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x + side)});
  const auto heading = screen.takeTop(std::max(48, line * 2), gap);
  const fui::Rect close{heading.x, heading.y, static_cast<int16_t>(std::max(72, line * 3)), heading.height};
  fui::ButtonProps button;
  button.label = tr(STR_BACK);
  button.action = Control;
  button.value = 8;
  button.text = screen.theme().smallText;
  button.inputMask = fui::InputTouch;
  button.minTouchSize = 0;  // never expand neighbouring hit regions
  fui::button(screen.frame(), close, button);
  auto title = heading;
  title.x += close.width + gap;
  title.width -= close.width + gap;
  text(screen.target(), title, tr(STR_SGM_TITLE), screen.theme().titleText);
  if (page == Page::Board) {
    drawBoard(screen);
    return;
  }

  if (page == Page::Rules) {
    const char* pages[] = {tr(STR_SGM_RULES_1), tr(STR_SGM_RULES_2), tr(STR_SGM_RULES_3)};
    const auto footer = screen.takeBottom(std::max(48, line * 2), gap);
    auto style = screen.theme().bodyText;
    style.maxLines = 24;
    screen.target().text(screen.contentRect(), pages[rulesPage], style);
    button.label = tr(STR_BACK);
    button.value = 0;
    button.state = selected == 0 ? fui::StateSelected : fui::StateNormal;
    auto half = footer;
    half.width = (footer.width - gap) / 2;
    fui::button(screen.frame(), half, button);
    half.x = footer.right() - half.width;
    button.label = tr(STR_SGM_NEXT_RULES);
    button.value = 1;
    button.state = selected == 1 ? fui::StateSelected : fui::StateNormal;
    fui::button(screen.frame(), half, button);
    return;
  }

  if (page == Page::Menu) {
    const char* names[] = {tr(STR_GAME_CONTINUE),   tr(STR_GOMOKU_NEW_AI_15), tr(STR_GOMOKU_NEW_2P_15),
                           tr(STR_GOMOKU_NEW_AI_9), tr(STR_GOMOKU_NEW_2P_9),  tr(STR_SGM_RULES)};
    for (int i = 0; i < 6; ++i) snprintf(labels[i], sizeof(labels[i]), "%s", names[i]);
  } else if (page == Page::Pause) {
    const char* names[] = {tr(STR_GAME_CONTINUE), tr(STR_SGM_SKILLS),    tr(STR_GOMOKU_UNDO),
                           tr(STR_SGM_RULES),     tr(STR_GOMOKU_RESIGN), tr(STR_EXIT)};
    for (int i = 0; i < 6; ++i) snprintf(labels[i], sizeof(labels[i]), "%s", names[i]);
  } else {
    const auto status = screen.takeTop(std::max(44, line * 2), gap);
    snprintf(labels[0], sizeof(labels[0]), tr(STR_SGM_ENERGY), game.energy[0], game.energy[1]);
    text(screen.target(), status, labels[0], screen.theme().bodyText);
    for (int i = 0; i < 7; ++i) {
      const auto skill = static_cast<Skill>(i + 1);
      const char* statusLabel = tr(STR_SGM_READY);
      char cooldownText[64];
      uint8_t cd = 0;
      if (i < 3) cd = game.cooldown[game.turn - 1][i];
      if (i >= 3 && i <= 5 && (game.used[game.turn - 1] & (1u << (i - 3))))
        statusLabel = tr(STR_SGM_USED);
      else if (cd) {
        snprintf(cooldownText, sizeof(cooldownText), tr(STR_SGM_COOLDOWN), cd);
        statusLabel = cooldownText;
      } else if (!game.available(skill))
        statusLabel = tr(STR_SGM_LOW_ENERGY);
      snprintf(labels[i], sizeof(labels[i]), tr(STR_SGM_SKILL_LABEL), skillName(skill), Game::cost(skill), statusLabel);
    }
    snprintf(labels[7], sizeof(labels[7]), "%s", tr(STR_CANCEL));
  }
  const auto area = screen.contentRect();
  const int columns = page == Page::Skills ? 2 : 1;
  const int rows = itemCount() / columns;
  const int height = std::min(std::max(56, line * (columns == 2 ? 4 : 2)), (area.height - gap * (rows - 1)) / rows);
  for (int i = 0; i < itemCount(); ++i) {
    const fui::Rect rect{static_cast<int16_t>(area.x + (i % columns) * ((area.width + gap) / columns)),
                         static_cast<int16_t>(area.y + (i / columns) * (height + gap)),
                         static_cast<int16_t>((area.width - gap * (columns - 1)) / columns),
                         static_cast<int16_t>(height)};
    button.label = labels[i];
    button.value = i;
    button.text = screen.theme().bodyText;
    button.text.maxLines = columns == 2 ? 3 : 2;
    button.state = selected == i ? fui::StateSelected : fui::StateNormal;
    button.enabled = !(page == Page::Menu && i == 0 && !hasSave);
    if (page == Page::Pause) {
      if (i == 1 || i == 4) button.enabled = !game.winner && !(versusComputer && game.turn == 2);
      if (i == 2) {
        int undoIndex = historyCount - 1;
        if (versusComputer)
          while (undoIndex >= 0 && history[undoIndex].turn != 1) --undoIndex;
        button.enabled = undoIndex >= 0;
      }
    }
    if (page == Page::Skills && i < 7) button.enabled = game.available(static_cast<Skill>(i + 1));
    fui::button(screen.frame(), rect, button);
  }
}

void SkillGomokuActivity::drawBoard(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int line = screen.target().lineHeight(theme.bodyText.font);
  const int gap = std::max(8, int(theme.spaceMd));
  const auto energyRect = screen.takeTop(line + gap, gap);
  snprintf(labels[0], sizeof(labels[0]), tr(STR_SGM_ENERGY), game.energy[0], game.energy[1]);
  text(screen.target(), energyRect, labels[0], theme.bodyText);
  const auto statusRect = screen.takeTop(line + gap, gap);
  const char* status = thinking         ? tr(STR_GOMOKU_AI_THINKING)
                       : game.turn == 1 ? tr(STR_SGM_BLACK_TURN)
                                        : tr(STR_SGM_WHITE_TURN);
  if (game.winner)
    status = game.winner == 1   ? tr(STR_GOMOKU_BLACK_WINS)
             : game.winner == 2 ? tr(STR_GOMOKU_WHITE_WINS)
                                : tr(STR_GOMOKU_DRAW);
  text(screen.target(), statusRect, status, theme.bodyText);
  const auto controls = screen.takeBottom(std::max(48, line * 2), gap);
  const auto hint = screen.takeBottom(line * 3, gap);
  const char* message = saveFailed ? tr(STR_SGM_SAVE_FAILED) : notice ? notice : skillHint(armed, source == Game::None);
  text(screen.target(), hint, message, theme.smallText, 3);

  const auto band = screen.contentRect();
  // Half-pitch padding leaves the outer intersections fully hittable and visible.
  const auto layout = GomokuLayout::fit(band.x, band.y, band.width, band.height, game.size);
  pitch = layout.pitch;
  if (!pitch) return;
  const int gridSide = pitch * (game.size - 1);
  boardX = layout.x;
  boardY = layout.y;
  const fui::Rect boardRect{static_cast<int16_t>(boardX - pitch / 2), static_cast<int16_t>(boardY - pitch / 2),
                            static_cast<int16_t>(pitch * game.size), static_cast<int16_t>(pitch * game.size)};
  if (!game.winner && !thinking && !(versusComputer && game.turn == 2))
    screen.frame().hit(boardRect, Board, 0, fui::InputTouch);
  const auto ink = fui::Paint::solid(fui::Color::Black);
  const auto white = fui::Paint::solid(fui::Color::White);
  if (armed == Skill::Wipe) {
    const int left = std::max(0, int(cursor % game.size) - 1);
    const int right = std::min(int(game.size) - 1, int(cursor % game.size) + 1);
    const int top = std::max(0, int(cursor / game.size) - 1);
    const int bottom = std::min(int(game.size) - 1, int(cursor / game.size) + 1);
    screen.target().stroke(
        {static_cast<int16_t>(boardX + left * pitch - pitch / 2),
         static_cast<int16_t>(boardY + top * pitch - pitch / 2), static_cast<int16_t>((right - left + 1) * pitch),
         static_cast<int16_t>((bottom - top + 1) * pitch)},
        ink, 2, 4);
  }
  for (int i = 0; i < game.size; ++i) {
    screen.target().line({static_cast<int16_t>(boardX), static_cast<int16_t>(boardY + i * pitch)},
                         {static_cast<int16_t>(boardX + gridSide), static_cast<int16_t>(boardY + i * pitch)}, 1, ink);
    screen.target().line({static_cast<int16_t>(boardX + i * pitch), static_cast<int16_t>(boardY)},
                         {static_cast<int16_t>(boardX + i * pitch), static_cast<int16_t>(boardY + gridSide)}, 1, ink);
  }
  if (game.winStart != Game::None && game.winEnd != Game::None) {
    screen.target().line({static_cast<int16_t>(boardX + (game.winStart % game.size) * pitch),
                          static_cast<int16_t>(boardY + (game.winStart / game.size) * pitch)},
                         {static_cast<int16_t>(boardX + (game.winEnd % game.size) * pitch),
                          static_cast<int16_t>(boardY + (game.winEnd / game.size) * pitch)},
                         3, ink);
  }
  const int radius = std::max(2, pitch * 3 / 8);
  for (int i = 0; i < game.size * game.size; ++i) {
    const int x = boardX + (i % game.size) * pitch, y = boardY + (i / game.size) * pitch;
    const fui::Rect stone{static_cast<int16_t>(x - radius), static_cast<int16_t>(y - radius),
                          static_cast<int16_t>(radius * 2 + 1), static_cast<int16_t>(radius * 2 + 1)};
    if (game.cells[i]) {
      screen.target().fill(stone, game.cells[i] == 1 ? ink : white, radius);
      if (game.cells[i] == 2) screen.target().stroke(stone, ink, 2, radius);
    }
    if (i == source)
      screen.target().stroke({static_cast<int16_t>(x - radius - 2), static_cast<int16_t>(y - radius - 2),
                              static_cast<int16_t>(radius * 2 + 5), static_cast<int16_t>(radius * 2 + 5)},
                             ink, 2);
    if (i == cursor && !thinking) {
      const int half = pitch / 2 - 1;
      screen.target().stroke({static_cast<int16_t>(x - half), static_cast<int16_t>(y - half),
                              static_cast<int16_t>(half * 2), static_cast<int16_t>(half * 2)},
                             ink, 2, 3);
    }
  }
  fui::ButtonProps button;
  button.label = armed == Skill::Place ? tr(STR_GOMOKU_PLACE) : skillName(armed);
  button.action = Control;
  button.value = 0;
  button.inputMask = fui::InputTouch;
  button.text = theme.bodyText;
  button.minTouchSize = 0;
  button.enabled = !thinking && !(versusComputer && game.turn == 2 && !game.winner);
  auto half = controls;
  half.width = (controls.width - gap) / 2;
  fui::button(screen.frame(), half, button);
  half.x = controls.right() - half.width;
  button.label = game.winner ? tr(STR_SGM_MENU) : tr(STR_SGM_SKILLS);
  button.value = 1;
  fui::button(screen.frame(), half, button);
}

void SkillGomokuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const auto hints = mappedInput.mapLabels(tr(STR_SGM_MENU), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
