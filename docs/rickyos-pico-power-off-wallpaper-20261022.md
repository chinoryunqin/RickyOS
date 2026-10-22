# Pico independent power-off wallpaper — 2026-10-22

Status: implemented locally, Pico build and host regressions passed; flashed on
the user's subsequent explicit request. Not committed, pushed or released.
Dictionary download/lookup
acceptance of the previously flashed owned-resource build remains separate.

## Interaction

- Apps → 屏保 → 关机 → 选择关机图片.
- Select a BMP/PNG/JPG/JPEG, inspect preview, then press 设为关机壁纸.
- Successful installation selects 自定义壁纸 automatically; cancel changes nothing.
- Standby continues using its own existing picture/style. “和待机一样” remains an
  explicit optional choice, not a prerequisite for custom power-off pictures.
- No new badges or status text are painted over this independent picture.
- Fit/filter preferences are still shared with the existing wallpaper renderer.

## Storage / compatibility

- Power-off picture: `/poweroff.bmp`; standby picture remains `/sleep.bmp`.
- Original source image is never edited. JPEG/PNG reuse the existing converted
  BMP preview; no additional full-screen image allocation.
- Append power-off saved ID 3; preserve IDs 0/1/2, defaults, migration and JSON key.
- Copy/open/read/write/rename/settings-save failures leave the prior selection
  and restore the previous image where filesystem operations succeed. A stale
  backup is recovered before the next installation attempt. FAT/power loss is
  not claimed atomic.
- Missing/invalid power-off image: default power-off screen, not standby fallback.

## Verification

- Pico `pio run -e rickyos_readpico`: success.
- Standby-related Python/compiled-source host regressions: 27 tests passed.
- New compiled production installer/options tests: 2 tests passed (BMP/JPEG/PNG
  routing, independent state in both directions, invalid-preview guard, failed
  reads/writes/open/rename/save, zero-length source, stale staging/backup).
- Native grayscale recording-HAL integration: passed, including independent
  power-off file, missing/invalid image fallback, original standby and filters.
- Power settings/model regressions: 15 tests passed (includes the 2 new installer
  tests). Image-only file picker regression: 1 test passed, including BMP/PNG/JPEG
  filtering; its stale library-picker enum/helper mock was updated without
  changing production file-browser logic.
- No timing/visual-quality claim based on host tests.

## Authorized trial installation

- Read Pico MAC `14:c1:9f:d1:9f:58`, ESP32-S3; not Plus.
- App binary SHA-256 `583c3b41a32adb7965ef5c5c44f16987c7e532f74b58cd8d4ed2a38d5c660b29`,
  5,933,952 bytes. Matching ELF SHA-256
  `11e50963bc2eff42d8a5814752880fd28c705849ab64471caf1274369a8a6ea0`.
- Exact previous owned-resource app checked before writing; only active app0 at
  `0x10000` written. Written digest verified; outside-app Flash hashes unchanged.
  SD card not accessed. Explicit USB reset requested; startup reached `InxRecent`.
- Ignored evidence directory:
  `.local/pico-storage-crash/install-independent-poweroff-round1/`.
- Image selection, power-off appearance and separation from standby still need
  user acceptance; startup logs do not establish those outcomes.

## Device acceptance still needed

Use two different pictures for standby and power-off; confirm each via its tab.
Enter standby, wake, power off and restart. Verify the independent selections
survive reboot, and changing either picture does not change the other. Also try
cancelling preview, a bad image and missing/read-only card; keep existing user
images backed up for destructive media-failure testing.

## Subsequent acceptance and release authorization

The user subsequently confirmed “可以了，发布吧” for this installed image and
explicitly confirmed “已测过，统计正常” for Storage → Refresh statistics. This
authorizes publication of the exact installed image as Pico 1.2.5, not a new
rebuild or a Plus/A4 update. Extended card-removal/power-loss cases remain
unverified; acceptance does not convert those into tested claims.
