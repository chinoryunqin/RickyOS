# EEGO A4 1.2.4 unpublished candidate — 2026-10-19

This candidate was superseded by 1.2.5 before any production offer was activated.
Its immutable binaries and private tag remain as build history. During publication
review, the A4 OTA entry was found to reject its own board; 1.2.5 also repairs that
check. See `eego-a4-1.2.5-release.md`.

## Acceptance

The device owner flashed `1.2.4-test.1` (5,993,952 bytes, SHA-256 `b004b53e71dc4382857f36f0d73dc99941dadefc91995a783915fc59fd8d4a17`) and confirmed: “我刷了，图片现在显示正常了，可以把这个修复也发布了。”

Production 1.2.4 preserves this accepted firmware behavior; only the version changes.
Production image: 5,993,888 bytes, SHA-256 `9722fa774904b726512d3531c829b2a67d90075b081777b5fa4a523372c2aaed`.
The bootloader, partition table and boot_app0 are byte-identical to A4 1.2.3;
existing complete-install acceptance carries forward for these unchanged startup
assets. The build host did not separately flash the production image.

## Result

The shared EPUB/WeRead reader keeps A4 illustrations in dithered B/W and presents
them with one cleanup refresh, matching the existing image-viewer workaround.
It skips the gray overlay that washed out the initially visible image. The next
text page keeps its cleanup debt. Other-device grayscale behavior is unchanged.
All accepted 1.2.3 interaction and network fixes remain included.

## Validation

- Production `rickyos_eego_a4` build succeeds.
- Image-refresh regression passes under ASan/UBSan for A4 hardware/simulator and
  peer-device configurations (128 combinations); reading cache 4 tests and reading
  UI 10 tests pass.
- Flasher tests: 78 pass. Production catalog checks firmware versions, board tags,
  chips, sizes, hashes and install segments; website build succeeds.
- Patch passes `git diff --check`.
- Evidence: `artifacts/eego-1.2.4-release-20261019/` outside this worktree; accepted
  test image and earlier reproduction evidence remain in
  `artifacts/eego-reader-images-20261019-v8/`.

## Publication

Publish immutable binaries before switching offers. Only `eego_a4` advances to
1.2.4; Pico and Plus remain at 1.2.2. The website and both A4 OTA sources must offer
the exact same production binary. Retire the image-test entry to the production
installer; its test manifest remains withdrawn and unapproved.

Private source remote: `RickyOS-dev`; release tag: `eego-v1.2.4`. Public repositories
receive built binaries and static site assets only.
