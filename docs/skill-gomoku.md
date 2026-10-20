# Skill Gomoku / 技能五子棋

Implementation branch: `codex/skill-gomoku`, private `chinoryunqin/RickyOS-dev`.
No firmware release or physical-device validation is implied by this document.

## Scope and evidence

The user explicitly requires the traditional game to remain unchanged.
`src/activities/apps/gomoku/GomokuBoard.h:31-32` derives the next player from
placement count, and lines 43-49 serialize alternating placement history.
Neither represents removal, conversion or skipped turns. The new engine is
independent; the traditional Board, AI and Store stay untouched.
`GomokuStore.cpp:8-15` owns the traditional version-1 save/stat paths. Skill
games never write those paths or count towards traditional statistics.
`GomokuMenuActivity.cpp:55-61` supplies the existing board/mode choices; these
remain under the new Traditional entry.

The user additionally requests a larger touch board. The existing
`GomokuGameActivity.cpp:148-160` restricts adaptive pitch to Read Pico and fixes
other boards to 30/50 pixels. Lines 170-177 add an extra half-pitch after
centering, while lines 638-640 put statistics directly above the action bar.
The layout-only change shares bounded fit geometry with the new game, reserves
space for the existing statistics, and applies adaptive sizing to touch boards.
Rules, AI and save formats of the traditional game remain unchanged.

Community references: https://www.jinengwuziqi.com/ and
https://skill-gomoku.world/ . There is no single standard ruleset. Skill names
reference the community meme; rules, implementation and UI here are our own.
No third-party art, audio or game code is copied.

## Rules

- Local two-player and human (black) versus computer (white), 9 or 15 lines.
- Black starts. Five or more in any straight direction wins; no forbidden moves.
- Each player starts with 3 energy, maximum 8. A normal placement adds 1.
- One successful action per turn: place OR cast. Invalid/cancelled actions do
  not consume energy, a turn, cooldown or once-per-game allowance.
- 飞沙走石 (3): remove one opposing stone.
- 偷梁换柱 (4): convert one opposing stone to your colour; may win.
- 移花接木 (2): move one own stone to an empty intersection; may win.
- These three have a cooldown of 2 subsequent successful actions by that player.
- 静如止水 (5, once/player/game): place a stone and skip the opponent's next
  turn. Does not give the normal placement energy bonus.
- 两极反转 (6, once/player/game): exchange every stone's colour. Players,
  energy and cooldowns do not exchange. Requires at least one of each colour.
- 力拔山兮 (8, once/player/game): remove all stones in a selected 3×3 area,
  clipped at board edges. Must remove at least one stone.
- 休养生息 (free): add 3 energy (capped at 8), end turn; unavailable at cap.
- Full board without five, or 400 successful actions without a winner: draw.
- Undo restores the entire pre-action state, including resources and turn.
  In AI games it returns to before the last human action, including AI replies.
  History is bounded to 8 actions in RAM and is not persisted across reload.

## Resource and input contract

Fixed-size engine, scratch and eight undo states live inside the already
heap-owned Activity, not on the small task stack. No board-sized dynamic
container, screen buffer, background task or effect animation. AI processes a
bounded candidate batch per loop and uses the same legal-action API as humans.
New screens use UiAppHost/FreeInkUI interaction routing. Touch aims at a board
intersection; a separate confirmation commits. Skill selection never casts.
Cancel restores placement mode without spending resources. Inherited button
releases are swallowed at transitions.

The save is separately versioned and uses two checked generations, because FAT
replacement itself is not power-loss atomic. The older slot is replaced via a
temporary file while the current good slot is retained. It uses explicit bytes
and CRC, not native struct layout. See file-formats.md.

## Validation

2026-10-20 verification:

- `python3 scripts/tests/test_skill_gomoku.py`: two compiled ASan/UBSan suites
  passed (rules/AI/layout and real Store with fault-injected host storage).
  Covers all skills, invalid-action immutability, cooldown/resources, all four
  win directions, full AI games on both sizes, codec corruption/truncation,
  failed FAT replacement and legacy-save isolation.
- Engine state is 248 bytes, computer workspace 396 bytes on the host. The
  eight undo states total 1,984 bytes, Activity-owned; no search tree/task/frame.
- Plus `bash tests/run_plus_port_tests.sh`: complete regression runner passed,
  including existing font pixel checks and the new game suites.
- Plus native build passed using the existing IDF 6.1-r/Arduino RC1 port,
  version marker `1.2.2-dev`. This is a build check, not a released version.
- Native browser simulator compiled successfully; isolated preview at
  `http://127.0.0.1:8786/`. It renders the actual Plus application through the
  Read Pico 684×1216 desktop HAL, not an S31 waveform/USB/PSRAM emulator. The
  user tried it and accepted the current iteration.
- Traditional rules/AI/Store remain unchanged; only the shared app entry and
  requested touch-board geometry, cursor sizing and stat-icon cap change.
- Source snapshot checks passed: 2,844 pinned files, 125 recorded adaptations.
  Use `scripts/sync_skill_gomoku.py --baseline-repo <initialized-repo>` to mirror
  shared game sources without changing the Plus pin; `--fonts` also copies the
  regenerated UI subsets. The Plus product body's existing frequency-grouped
  12pt font is retained. Root subsets were regenerated with the pinned official
  Noto OTFs and existing generators; no font bytes were hand-edited.

New screens use the shared FreeInkUI host and routing. Board touches aim only;
commit is a separate action. Rules are three pages, and physical buttons can
reach skills through the pause menu. UI cancel leaves resources unchanged.
Sleep saves without taking a second render lock (the manager owns it).

Physical touch accuracy, display latency, RAM watermark and hardware sleep/save
behavior still require device testing. No serial port, device SD or flashing
was used. All work is local/private; no stable branch, release or site changed.
