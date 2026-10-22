# Read Pico 1.2.5 — 2026-10-22

## Scope and authorization

Only Read Pico advances from 1.2.2 to 1.2.5. Preserve the current Plus 1.2.5
and EEGO A4 1.2.5 catalog entries and OTA files without modification. Public
repositories receive static site files and binaries, not this private source.
Private source branch/tag: `codex/pico-release-1.2.5` / `pico-v1.2.5`.

The user accepted the installed combined image, requested publication, and
explicitly confirmed Storage → Refresh statistics completes normally. The
exact accepted binary is reused without a release rebuild or version change.
Its runtime already reports 1.2.5.

## Image

- App bytes: 5,933,952 (below the 6,029,312-byte OTA safety limit).
- SHA-256: `583c3b41a32adb7965ef5c5c44f16987c7e532f74b58cd8d4ed2a38d5c660b29`.
- ELF SHA-256: `11e50963bc2eff42d8a5814752880fd28c705849ab64471caf1274369a8a6ea0`.
- Read Pico MAC `14:c1:9f:d1:9f:58`; active app0 installation, digest check,
  protected Flash hash check, explicit USB reset and `InxRecent` startup passed.
- Retain existing accepted 1.2.2 full-install components; rebuilt bootloader
  and partitions are byte-identical. `boot_app0.bin` is the same pinned Arduino
  component. No new bootloader/partition/SD migration is introduced.

The release snapshot contains library/storage `70740e22`, Pico battery
`e1c58b3f`, the complete book-style content integration through `301d8e4b`,
storage summary interaction removal and smaller statistics scratch, owned
dictionary/fallback-font routes, and independent power-off image selection.

## Device-facing notes

1. 书库支持指定文件夹、单本收录和自建分类
2. 新增列表视图、拼音首字母搜索和页码跳转
3. 优化大书库使用，存储统计区不再打开清缓存
4. 优化电量估算与显示，不改变充电参数
5. 优化 EPUB 精排样式、底色和段落显示
6. 关机壁纸支持单独选图，不影响待机壁纸
7. 词典和缺字字库改用自有资源服务
8. 更新后请在书库添加原有书籍目录，SD 卡书籍不删除

## Verification and limits

Pico firmware build, 191 library/parser/dictionary C++ checks, battery policy
8, battery characterization 2, storage 9, reader page cache 4, reader resources
3, font catalog 10, library workflow 8 and COS publisher 11 checks passed in
the release-preparation run. Wallpaper standby tests (27), power tests (15)
and file-picker test (1), plus native grayscale recording-HAL integration,
passed during implementation. Native tests are not physical panel proof.
The flasher's 78 tests, full release/install-asset validation and site build
passed. Production updater/trust C++ tests (Pico, A4 identity and downloader)
and 3 TLS-source checks also passed; no new physical OTA run is claimed.

The statistics result is user-observed; the original crash root cause and
worst-case stack headroom were not established. Battery curve calibration,
long discharge tests, card removal/power loss and all dictionary download
failure cases are not newly claimed accepted. Keep manual dictionary imports
and existing owned-resource markers intact.

## Publication

Prepare from fresh official website/OTA clones, check the accepted app with
the flasher's real release/install policy and tests. Stage immutable files on
the website, GitHub Pages OTA site and COS first, verify their public bytes,
then activate notes/catalogs. The release directory is
`.local/pico-release125/` (ignored); keep plans, deployment heads, acceptance
and full public length/SHA-256 checks there. Preserve old firmware for rollback.
