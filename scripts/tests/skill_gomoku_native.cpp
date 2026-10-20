#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "GomokuLayout.h"
#include "SkillGomoku.h"

using namespace skillgomoku;

static void same(const Game& a, const Game& b) {
  uint8_t first[256], second[256];
  a.encode(first);
  b.encode(second);
  assert(!std::memcmp(first, second, sizeof(first)));
}
static void put(Game& game, uint8_t point) { assert(game.apply({Skill::Place, point})); }
static void patchCrc(uint8_t* bytes) {
  uint32_t crc = Game::checksum(bytes, 252);
  for (int i = 0; i < 4; ++i) bytes[252 + i] = crc >> (i * 8);
}
static void resources() {
  Game game;
  game.reset(9);
  put(game, 40);
  put(game, 41);
  assert(game.turn == 1 && game.energy[0] == 4);
  assert(game.apply({Skill::Remove, 41}));
  assert(game.cells[41] == 0 && game.energy[0] == 1 && game.cooldown[0][0] == 2 && game.turn == 2);
  put(game, 42);
  Game before = game;
  assert(!game.apply({Skill::Remove, 42}));
  same(before, game);
  assert(!game.apply({Skill::Convert, 42}));
  same(before, game);
  put(game, 30);
  assert(game.cooldown[0][0] == 1);
  put(game, 50);
  assert(game.apply({Skill::Rest}));
  assert(game.cooldown[0][0] == 0 && game.energy[0] == 5);
  put(game, 60);
  assert(game.apply({Skill::Remove, 42}));
  assert(game.cells[42] == 0);
  game.energy[game.turn - 1] = 7;
  assert(game.apply({Skill::Rest}));
  assert(game.energy[2 - game.turn] == 8);
  game.energy[game.turn - 1] = 8;
  before = game;
  assert(!game.apply({Skill::Rest}));
  same(before, game);
}
static void powers() {
  Game game;
  game.reset(9);
  game.energy[0] = 8;
  assert(game.apply({Skill::Freeze, 40}));
  assert(game.turn == 1 && game.energy[0] == 3 && game.used[0] == 1);
  game.energy[0] = 8;
  assert(!game.apply({Skill::Freeze, 41}));
  put(game, 41);
  put(game, 42);
  const auto own = game.cells[40];
  assert(game.apply({Skill::Reverse}));
  assert(game.cells[40] == 3 - own && game.used[0] == 3 && game.turn == 2);
  game.energy[1] = 8;
  game.cells[0] = 1;
  game.cells[1] = 2;
  game.cells[9] = 1;
  game.cells[10] = 2;
  assert(game.apply({Skill::Wipe, 0}));
  assert(!game.cells[0] && !game.cells[1] && !game.cells[9] && !game.cells[10]);
  assert(game.cells[40] && game.used[1] == 4 && game.energy[1] == 0);
  Game blank;
  blank.energy[0] = 8;
  assert(!blank.apply({Skill::Reverse}));
  assert(!blank.apply({Skill::Wipe, 0}));
  assert(!blank.apply({Skill::Move, 0, 0}));
}
static void wins() {
  const int steps[] = {1, 9, 10, 8};
  const int starts[] = {18, 2, 0, 8};
  for (int direction = 0; direction < 4; ++direction) {
    Game game;
    game.reset(9);
    for (int i = 0; i < 4; ++i) game.cells[starts[direction] + i * steps[direction]] = 1;
    game.actions = 4;
    game.lastActor = 2;
    assert(game.apply({Skill::Place, static_cast<uint8_t>(starts[direction] + 4 * steps[direction])}));
    assert(game.winner == 1 && game.winStart != Game::None && game.winEnd != Game::None);
    auto before = game;
    assert(!game.apply({Skill::Rest}));
    same(before, game);
    uint8_t encoded[256];
    game.encode(encoded);
    Game decoded;
    assert(decoded.decode(encoded, sizeof(encoded)));
    same(decoded, game);
  }
  Game convert;
  convert.reset(9);
  for (int i = 0; i < 4; ++i) convert.cells[i] = 1;
  convert.cells[4] = 2;
  convert.energy[0] = 4;
  assert(convert.apply({Skill::Convert, 4}) && convert.winner == 1);
  Game move;
  move.reset(9);
  for (int i = 0; i < 4; ++i) move.cells[i] = 1;
  move.cells[20] = 1;
  assert(move.apply({Skill::Move, 4, 20}) && move.winner == 1 && !move.cells[20]);
  Game resign;
  assert(resign.apply({Skill::Resign}) && resign.winner == 2);
  uint8_t encoded[256];
  resign.encode(encoded);
  Game decoded;
  assert(decoded.decode(encoded, 256));
}
static void codec() {
  Game game;
  game.reset(9);
  put(game, 40);
  put(game, 41);
  assert(game.apply({Skill::Move, 30, 40}));
  uint8_t encoded[256];
  game.encode(encoded);
  Game decoded;
  assert(decoded.decode(encoded, 256));
  same(game, decoded);
  const Game before = decoded;
  for (int length = 0; length < 256; ++length) {
    assert(!decoded.decode(encoded, length));
    same(before, decoded);
  }
  for (int byte = 0; byte < 256; ++byte) {
    encoded[byte] ^= 1;
    assert(!decoded.decode(encoded, 256));
    same(before, decoded);
    encoded[byte] ^= 1;
  }
  const int offsets[] = {4, 5, 6, 7, 9, 11, 17, 21, 24, 25, 250};
  for (int offset : offsets) {
    game.encode(encoded);
    encoded[offset] = 254;
    patchCrc(encoded);
    assert(!decoded.decode(encoded, 256));
    same(before, decoded);
  }
  game.reset(9);
  for (int i = 0; i < 400; ++i) {
    game.energy[game.turn - 1] = 0;
    assert(game.apply({Skill::Rest}));
  }
  assert(game.winner == 3);
  game.encode(encoded);
  assert(decoded.decode(encoded, 256));
}
static void ai() {
  Game game, scratch;
  game.reset(9);
  game.turn = 2;
  game.energy[1] = 0;  // only placement available; immediate block
  for (int i = 0; i < 4; ++i) game.cells[i] = 1;
  game.actions = 4;
  game.lastActor = 1;
  Computer computer;
  computer.begin(game);
  int passes = 0;
  while (!computer.step(game, scratch)) assert(++passes < 33);
  assert(computer.result().skill == Skill::Place && computer.result().target == 4);
  for (int i = 0; i < 4; ++i) game.cells[i] = 2;
  computer.begin(game);
  while (!computer.step(game, scratch)) {
  }
  assert(game.apply(computer.result()) && game.winner == 2);
  // Complete games, both sizes, same fair public action API. Every resulting
  // checkpoint must round-trip. Bounds/no infinite AI thinking are asserted.
  for (int size : {9, 15}) {
    game.reset(size);
    for (int action = 0; action < 400 && !game.winner; ++action) {
      computer.begin(game);
      assert(computer.candidateCount() > 0 && computer.candidateCount() <= 128);
      passes = 0;
      while (!computer.step(game, scratch)) assert(++passes < 33);
      assert(game.legal(computer.result()));
      assert(game.apply(computer.result()));
      uint8_t encoded[256];
      game.encode(encoded);
      assert(scratch.decode(encoded, 256));
      same(game, scratch);
    }
    assert(game.winner);
  }
}
static void layout() {
  for (int width : {480, 684, 720}) {
    for (int height : {800, 1216, 1280}) {
      for (int size : {9, 15}) {
        const auto fit = GomokuLayout::fit(16, 160, width - 32, height - 400, size);
        assert(fit.pitch > 0);
        assert(fit.x - fit.pitch / 2 >= 16 && fit.y - fit.pitch / 2 >= 160);
        assert(fit.x + (size - 1) * fit.pitch + fit.pitch / 2 <= width - 16);
        assert(fit.y + (size - 1) * fit.pitch + fit.pitch / 2 <= height - 240);
      }
    }
  }
  assert(GomokuLayout::fit(16, 60, 688, 950, 15).pitch == 45);
  assert(GomokuLayout::fit(16, 60, 688, 950, 9).pitch == 76);
  assert(!GomokuLayout::fit(0, 0, 0, 0, 15).pitch);
}
int main() {
  resources();
  powers();
  wins();
  codec();
  ai();
  layout();
  std::printf("Skill Gomoku native tests passed; Game=%zu, Computer=%zu bytes\n", sizeof(Game), sizeof(Computer));
}
