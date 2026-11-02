# 1.2.14 release preparation, 2026-11-02

Candidate scope: Pico and Plus share one Reset All Bindings row in Bluetooth button mapping. Confirmation defaults to Cancel; confirmation clears/saves custom mappings, keeping paired devices. Existing function rows still enter capture directly. No individual-clear feature was added.

Plus additionally includes the committed standby repairs (b8acf8c6): inherited Wi-Fi cleanup, retained-SD shallow CPU sleep fallback, sleeping missing-picture prompt, and ignored passive input no longer delaying idle sleep. Pico is based on its own official pico-v1.2.13; Plus is based on plus-v1.2.13. Neither candidate includes unrelated development work.

Both application images were built as 1.2.14 and passed image checksum/hash, exact chip/board identity and 512 KiB OTA guard checks. Plus disables the temporary POWER_CAPTURE/STANDBY_DIAG/FIELD_DIAG/POWER_LOG flags. Pico host regressions: 15; Plus/shared host regressions: 14, under ASan/UBSan. Physical interaction/OTA acceptance is tracked separately in the adjacent JSON; no publication has occurred.

Image identities:

- pico: RickyOS-1.2.14.bin, 5961488 bytes, SHA256 be084fa7a8d8b914b26082bde2eded56583385350313a6cc193264ce3fdcb5a9; aligned usable headroom 65536 bytes.

- plus: RickyOS-Plus-1.2.14.bin, 5984336 bytes, SHA256 c806b7f3fa05682b8ac770aa035f5bcac67dc679f8524f82ab1fb3db043edd41; aligned usable headroom 40960 bytes.

Release authorization: user explicitly said “没啥问题，可以直接发了” for both targets. Plus was installed, startup/reader frame verified, and user accepted it. Pico 1.2.14 was not flashed or functionally tested on a physical device in this session; publication proceeds under this explicit maintainer authorization after that limitation was stated. The catalog hardwareAccepted gate records release acceptance; it does not claim a new Pico hardware or OTA installation test. Existing accepted bootloader/partition segments are reused unchanged. Both binaries are the same hashes validated before publication; no rebuild is performed for the release commits.
