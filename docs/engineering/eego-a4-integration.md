# EEGO A4 integration candidate (2026-10-17)

The FreeInk SDK is RickyOS source in `freeink-sdk/` (vendored from upstream
`96de1be6` with our display, UI, storage, key and TLS changes, 2026-10-19). The A4
driver and its recording-bus host test live there directly; no build step patches
the SDK any more.

```sh
cmake -S test -B build/test -G Ninja
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j 6
```

## Layout and input

`RickyCompactLayout.h` supplies A4-only, stack-based layout geometry. Home
reserves the original puppy illustration, continue-reading card, statistics and motto; it has no bottom recent-books shelf. The A4 card follows the selected book directly (one book per page), so side-key movement never selects an invisible shelf item. Settings
uses two columns in portrait and three in landscape, with horizontal icon/label
cards. Both painting and hit registration use these rectangles.

A4 statistics shows the complete calendar; badges/month reports use the existing
paged list navigation. A4 `UiAppHost` has 40 action slots (512 additional fixed
bytes versus 24), sufficient for 31 days, two month arrows and two detail links.
Other devices retain 24 slots. Badge rendering follows selection inversion and
omits partial trailing rows so the art and text share the same viewport.

A4 top key: a debounced short click is logical Back inside a page, Standby on Home, and wake from Standby. Holding it for 2 seconds still powers off. Existing boot/wake release guards and the Power+Down screenshot owner precede short-click routing. Back publishes one complete shared-frame event with zero held duration; it cannot also Confirm or force-refresh from an old short-power preference. Root tabs return to Home directly, without the front-button focus step. Side keys keep page turns/navigation. The Keys settings page describes the actual top/side keys and omits the nonexistent middle key. No new heap storage is used (one fixed input flag).

The A4 preview's Back command emits the supported left-edge return gesture;
Home remains the physical Home action. `--ui-orientation 0..3` is an A4 simulator
layout aid and does not add global device auto-rotation. Reader orientation
continues through the normal settings path.

## Pinned display driver adapter

The A4 driver (`freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279cA4Driver.*`)
carries the adapter directly.

The adapter ports relevant behavior from public SDK commit
`f732b0798386da0bcef97a9ee7e69138612c437f` to the pinned transaction API: gray
power/VCOM, LSB-to-DTM1/MSB-to-DTM2, first-frame initialization, inverse baseline
after gray, and B/W baseline synchronization. It retains the existing four-fast
refresh cadence. Optional 52,992-byte gray planes use PSRAM only; frame inversion
uses a 96-byte row buffer. No new vendor fast LUT was inferred or substituted.

## Verification and reproduction

Run hardware/native builds sequentially; PlatformIO platform switching can
invalidate the other environment's output. Copy delivery binaries outside `.pio`
before switching.

```sh
pio run -e rickyos_eego_a4
pio run -e simulator_rickyos_eego_a4
python3 tools/rickyos-browser/check_eego_preview.py --out build/eego-preview
python3 scripts/tests/test_rickyos_browser.py
ctest --test-dir build/test --output-on-failure -j 8
```

The preview check uses disposable SD cards and covers both root orientations,
six settings categories and returns, stats, badge paging, month reports, tabs,
reader/menu, empty home, long title and nickname. Top-key checks cover all six settings children, root tabs, reader/menu, control center and Home standby/wake. The power-owner host test runs real statements for wake holds, long holds, debounce and context routing on A4 and legacy devices. CTest includes A4 layout
ASan/UBSan checks, adapter drift/idempotence and recording-bus driver tests.

Compilation, transactions and native frames do not validate physical panel
contrast/ghosting, touch calibration, network/SD performance or suspend power.
Before installation, identify the connected device and preserve its actual Flash
partition table/data. Then validate cold start, navigation/reading, gray-to-B/W
transitions, refresh cadence, warm/cool light persistence and sleep/wake on A4.

## Reader illustrations (2026-10-19 follow-up)

The accepted image-viewer workaround in `07ce4bfc` already keeps A4 images in
B/W because the overlay gray pass washes out a completed picture. The EPUB
reader still enabled that pass whenever a page had images, even with text AA
disabled. WeRead cached books use the same reader. A4 reader pages now keep
the dithered B/W illustration and use one clean HALF refresh on each image page;
the next text page retains its cleanup debt. Text-only refresh cadence is unchanged.
The other devices keep their existing grayscale paths. This changes no panel LUT,
cache format, allocation or network behavior.

`test_eego_reader_images.py` runs production refresh decisions and whole-plane
image-pass statements with ASan/UBSan across A4 hardware/simulator and peer-device
configurations, including text AA, night mode, images, manual refresh and cleanup
debt. The regression fails on 1.2.3's extra gray pass and passes after the fix.
The device owner flashed `1.2.4-test.1` and confirmed on 2026-10-19 that images
now display normally, authorizing the image-fix release. Reproduce by opening the same
illustrated pages in EPUB and WeRead, waiting for the refresh to settle, then
turning away and back. Release evidence is in `eego-a4-1.2.5-release.md`.
