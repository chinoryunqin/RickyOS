# EEGO A4 1.2.3 release — 2026-10-19

## Acceptance and scope

The device owner tested `1.2.3-test.7` and approved release: “感觉没啥问题，可以发布了”. Accepted image SHA-256: `1f0f817a107b1073da3b7c95c1fd8b77b48cae1c88826df2526743fcdedfc041` (5,997,936 bytes). The production build uses the same firmware behavior, with version `1.2.3`. Only the reading UI test's stale error enum/string fixture was corrected afterward; this does not enter the firmware.

Production image SHA-256: `54ae5e863930f6f905ae5309328b5dccf1fc9da1a775da93b737042667624dc1` (5,997,888 bytes). The ESP32-S3 bootloader, partition table, and boot_app0 are byte-identical to A4 1.2.2. Full-install acceptance carries forward for these unchanged startup assets. The production image was not separately flashed to hardware by the build host.

Release changes:

- Preserve the original back arrow appearance; expand its invisible contact region and reserve a gap before the first row. Keep touch ownership from the initial contact.
- Open the A4 control center only from a downward pull beginning in the status strip.
- Keep WiFi password letter case consistent with the keyboard; support cancellation during joining and improve association/IP timeout handling.
- Fit the profile settings and allow reader font-list scrolling.
- Allow cancellation while WeRead loads, distinguish error causes, and preserve the idle timeout without an absolute limit that interrupts progressing downloads.

## Validation

- Production `rickyos_eego_a4` build succeeds.
- Targeted A4 contact (2), control-center (2), WiFi lifecycle/startup (5), WeRead networking/cancellation (9), and reading regressions (10) pass.
- Flasher tests: 78 pass. Production catalog checks all firmware identities, sizes, hashes, chips and installation segments; build succeeds.
- Changed C++ files pass clang-format and the patch passes `git diff --check`.
- Earlier test.7 evidence, native navigation checks and screenshots are retained in `artifacts/eego-wifi-join-20261019-v7/` outside this worktree; production build and publication evidence are retained in `artifacts/eego-1.2.3-release-20261019/`.

## Publication

The catalog updates only `eego_a4` to 1.2.3. Pico and Plus remain at 1.2.2. Upload versioned binaries before activating the catalog or OTA offers. Publish A4 offers to the main website, legacy GitHub Pages OTA mirror and primary COS bucket. Retire old A4 test entry points to the formal installer; test manifests remain unapproved.

Private source remote: `RickyOS-dev`; release tag: `eego-v1.2.3`. Public repositories receive only static site assets and built binaries.
