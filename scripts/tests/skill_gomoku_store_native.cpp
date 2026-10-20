#include <HalStorage.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "SkillGomokuStore.h"

using namespace skillgomoku;
static constexpr const char* B = "/.crosspoint/skill_gomoku_v1_b.bin";
int main() {
  Storage.files["/.crosspoint/gomoku.bin"] = {1, 2, 3, 4};
  Storage.files["/.crosspoint/gomoku_stats.bin"] = {5, 6};
  const auto legacy = Storage.files;
  Game game, output;
  game.reset(9);
  bool ai = false;
  uint8_t cursor = 0;
  SkillGomokuStore store;
  assert(!store.load(output, ai, cursor));
  assert(game.apply({Skill::Place, 40}));
  assert(store.save(game, true, 40));
  assert(game.apply({Skill::Place, 41}));
  assert(store.save(game, true, 41));
  auto valid = Storage.files;
  SkillGomokuStore reader;
  assert(reader.load(output, ai, cursor) && output.actions == 2 && ai && cursor == 41);
  Storage.failWrite = true;
  assert(game.apply({Skill::Rest}));
  assert(!store.save(game, true, 41));
  assert(reader.load(output, ai, cursor) && output.actions == 2);
  Storage.failWrite = false;
  Storage.failReplace = true;
  assert(!store.save(game, true, 41));
  assert(reader.load(output, ai, cursor) && output.actions == 2);
  Storage.failReplace = false;
  // Every truncation and single-byte corruption of the newest generation
  // recovers the previous one. No partial state/metadata is published.
  for (size_t size = 0; size < 272; ++size) {
    Storage.files = valid;
    Storage.files[B].resize(size);
    assert(reader.load(output, ai, cursor) && output.actions == 1 && cursor == 40);
  }
  for (size_t byte = 0; byte < 272; ++byte) {
    Storage.files = valid;
    Storage.files[B][byte] ^= 1;
    assert(reader.load(output, ai, cursor) && output.actions == 1);
  }
  Storage.files = valid;
  assert(store.save(game, true, 41));
  assert(reader.load(output, ai, cursor) && output.actions == 3);
  assert(Storage.files.at("/.crosspoint/gomoku.bin") == legacy.at("/.crosspoint/gomoku.bin"));
  assert(Storage.files.at("/.crosspoint/gomoku_stats.bin") == legacy.at("/.crosspoint/gomoku_stats.bin"));
  assert(!store.save(game, false, 255));
  std::puts("Skill Gomoku save tests passed (FAT interruption, CRC, truncation, legacy isolation)");
}
