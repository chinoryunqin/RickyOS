# EEGO A4 1.2.5 release — 2026-10-19

The device owner flashed `1.2.4-test.1` and confirmed images display normally,
authorizing publication: “我刷了，图片现在显示正常了，可以把这个修复也发布了。”
Accepted image: 5,993,952 bytes, SHA-256
`b004b53e71dc4382857f36f0d73dc99941dadefc91995a783915fc59fd8d4a17`.

## Result

A4 EPUB and WeRead illustrations keep the dithered B/W frame with one clean
refresh; the washed-out grayscale overlay is skipped. All 1.2.3 touch/network
fixes remain included.

Publication review also found `OtaUpdater::checkForUpdate` required the literal
`readpico` board. The check now compares the firmware board tag with its own
`ricky_ota::BOARD`, admitting A4 without admitting another board. The COS-first
route, pinned certificate roots, SHA-256 and foreign-image checks remain intact.
The 1.2.4 candidate had already been uploaded as immutable files, so it was
superseded before any offer was activated; public 1.2.3 advances directly to 1.2.5.

Old A4 firmware (including the accepted test) still contains the old entry check
and requires a one-time website/USB update. This release repairs the updater for
subsequent in-device updates. Physical OTA installation was not performed on the
build host; the added OTA correction is verified with production-code host tests.

Production image: 5,993,872 bytes, SHA-256 `194306dd40b850fbc084c4b8579da233f3b8a9c93e2a0414b92fefa711c279a4`.
Startup components are byte-identical to the accepted 1.2.3 install package, so its
complete-install acceptance carries forward. No separate production hardware
flash is claimed.

## Validation

- Production A4 firmware build and full install-package checks pass.
- A4 image regression (128 combinations), reader cache (4), reading UI (10), and
  flasher (78) checks pass in this publication run.
- New A4/Pico route regression passes under ASan/UBSan, including own-board,
  foreign-board, COS priority, fallback, stale state and optional notes.
- Full production parser/updater, real host SHA-256 and simulated Flash: 407 checks
  pass each on Pico and A4; downloader certificate/redirect trust tests also pass.
- `git diff --check` passes. Existing host fixtures now find RickyPerf.h and supply
  the micros() timing seam; these repairs do not enter the firmware.

Only A4 advances. Pico/Plus remain at 1.2.2. Upload immutable files before switching
the website and two OTA offers. Retire the image-test entry to the formal installer;
keep its test manifest withdrawn and unapproved.

Private source tag: `eego-v1.2.5` on `RickyOS-dev`. Evidence is saved under
`artifacts/eego-1.2.5-release-20261019/` outside this worktree. Public repositories
receive built binaries and static site assets only.
