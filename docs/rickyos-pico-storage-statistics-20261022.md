# Read Pico storage statistics investigation — 2026-10-22

Local trial: `codex/pico-library-battery-trial`, based on `70740e22` with
the uncommitted battery integration from `e1c58b3f`. Not a release or a
hardware-accepted fix.

## Subsequent UI scope clarification

The user explicitly requested only removing the storage summary's cache-cleanup
interaction, not removing statistics, refreshing, or capacity display. The summary
now has no touch target or button-navigation slot; the three functional rows are
renumbered and the header Refresh remains. Settings cache cleanup is unchanged.
Nine storage and eight library workflow host checks passed after this UI change;
it has not been flashed. The frozen candidate below predates this UI change and
must not be described as containing it. The statistics crash remains unverified.

## Report and evidence

The user reports a restart after tapping Storage → Statistics on the library
and battery trial. The installed trial application was 5,924,800 bytes, SHA256
`03b02ae84fe6eabd90ccda49710bedac414bc50fa68c040bf27224d9bdabecca`.

The first read-only serial capture contains `RickyStorage` and `ClearCache`
activity entries, but no statistics completion, panic or restart. It does not
establish the failing instruction or task. No cache deletion was requested by
the agent.

A second no-reset serial capture contains ROM download-mode resets, binary
protocol traffic, then repeated bootloader errors: app0 has an invalid segment
length and app1 is not bootable. This is a separate application-image integrity
problem, not evidence of a statistics-task panic. No Flash write was executed
by this investigation. The serial capture was stopped to release the port.
Coordinate exclusive device access before recovery; do not overwrite an
unexpected application without resolving ownership with the user.

Raw serial captures and binaries remain local under `.local/pico-storage-crash/`.

## Candidate safety change

`RickyStorageActivity::libraryBytes()` originally placed 32 × 128-byte records
on a 6,144-byte statistics-task stack, before nested filesystem calls. This is
a stack-budget risk, not yet a confirmed cause of the reported crash.

The candidate uses eight records (1,024 bytes), reducing that local scratch
by 3,072 bytes without additional heap allocation or a larger task stack.
The same number of index bytes is read in smaller batches; no speed improvement
is claimed. Phase logs include the task's stack high-water mark before/after
free-space measurement, books, cache, fonts and saving. Directory depth and
cancellation behavior are unchanged.

## Earlier safety candidate verification (before the integrated trial)

- Read Pico firmware build succeeded.
- Eight storage host checks passed before the final rerun, including the actual
  extracted production byte-summing function under ASan/UBSan. Coverage includes
  0–16,384 books, partial batches, 64-bit sums, cancellation, open/allocation
  failure, and a maximum 1,024-byte read batch.
- The same batch-bound check fails against the old production function; this
  is not a device stack-overflow reproduction.
- That earlier standalone candidate was not flashed or physically accepted.

Frozen candidate: 5,924,928 bytes, SHA256
`f973496c2bca5ab3f9b86a0e915c2dac9a067aa1b91db24286f557d0f1840437`.
Matching ELF SHA256:
`96f69ad5e834c74abefffe2a6d8193b5d3bb482ed43bf662bb57508f5c743076`.

## Integrated Pico trial — 2026-10-22

The user subsequently requested integrating book styles before flashing. The
complete 14-file content difference through private `codex/pico-book-styles`
`301d8e4b` was applied on top of the existing library/battery/UI work without
overwriting staged battery changes. This is local content integration, not a
published merge commit. Five style commits are included: `6cc6f6ec`, `bc0a415a`,
`f27abb7f`, `29adad22`, `301d8e4b`.

The image contains library sources/categories, Pico battery filtering, the
statistics scratch reduction/phase logs, display-only summary interaction
(no cache-cleanup action), and the CSS panel/layout/gray-cache changes. Header
Refresh and the existing counting behavior remain as the user clarified.
It does not contain the separate reader-resource migration from the Plus worktree.

Read Pico firmware build passed. CSS tests 20, chapter/section tests 33,
upstream-parser fixtures 23, page-cache tests 4 and storage checks 9 passed.
The existing parser and cache host fixtures lacked the new panel drawing API;
their stubs were updated to compile the actual production paths. The parser
stub does not render pixels, so its pass does not establish panel appearance.

Frozen application: 5,932,944 bytes, SHA256
`710094c34ee4d6307811228b7098e0b240a98b22cce8f3635ec8ad62b9ca1317`.
ELF SHA256:
`08159916d57cbdd18ca703790cb0ca9886c7b92623faf2958456c90f46f5dc6a`.
Installation evidence is local in `.local/pico-storage-crash/install-integrated-round1/`;
check `installation.json` and `boot.log` for the actual write/readback/reset result.
No hardware acceptance or release may be inferred solely from the build/tests.

The user-authorized integrated image was subsequently installed on the known
Pico (`14:c1:9f:d1:9f:58`), app0 at `0x10000`. Application digest verification
and the protected Flash range comparison passed, followed by an explicit USB
reset and normal `read_pico` / version `1.2.5` / `InxRecent` startup. The SD card,
bootloader, partition table and OTA metadata were not written. Serial was
released after the bounded startup capture. Physical summary-touch, statistics
and book-panel acceptance remains pending; this is not a public release.

## Remaining hardware acceptance

After exclusive access and application recovery, capture a single statistics
request end-to-end. Require all phase logs, no panic/watchdog, and at least
512 bytes stack headroom. Repeat statistics, leave during counting, reenter,
and check ordinary library/reader/battery behavior. Retain the exact application
hash and ELF for any backtrace. Do not publish until the reported action has
actually passed on Pico; shared-source Plus behavior is not implied by this test.

## Subsequent user acceptance — Pico 1.2.5

After installing the integrated resource/wallpaper candidate, SHA-256
`583c3b41a32adb7965ef5c5c44f16987c7e532f74b58cd8d4ed2a38d5c660b29`,
the user requested publication and explicitly answered “已测过，统计正常” to
the storage-statistics completion/no-restart check. This is user-observed
functional acceptance of the current build. No end-to-end statistics serial
trace or minimum-stack measurement was captured, so the original crash cause
is still not proven. The smaller scratch remains a preventive stack-budget
change, not a conclusively diagnosed crash fix.
