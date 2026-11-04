# RickyOS 1.2.16 release preparation

Pico and Plus keep their separate images and existing boot/partition assets.
Final image hashes and physical acceptance are in
`release-1.2.16-verification-20261104.json`. Reading image profiles include
contrast, calibrated brightness and nearest/bilinear scaling, with source-image
preview, original-tone comparison, apply/cancel and per-book/default persistence.
CSS panels retain their parsed luminance. The main reader menu exposes image
adjustments and bookmarks and removes night mode/frontlight entries.

Both application images were backed up, serial-installed, verified and booted
normally. Flash outside the running application slot was unchanged. The maintainer
accepted the final menu, image effects/persistence, EPUB panel text, page turns
and standby/wake on Pico and the final menu and retained image/panel behavior on
Plus. Native renderer, panel geometry, parser pagination, reader/page-cache and
menu host regressions passed. Website tests (78) and the release build passed.
No new real OTA installation or overnight-current acceptance is claimed.

Plus was built with explicit version override 1.2.16; its default CMake version
was finalized to the same value after that build and tested both with/without
the override. The accepted image is retained byte-for-byte. No PDF, diagnostics,
new partition layout or changed OTA reserve is included. Raw serial captures,
device identities, flash backups, ELFs and private source patches remain local.

Publication completed on the brand website, GitHub OTA mirror and COS. All six
public applications and the rolling offers/notes were read back and matched the
accepted images and single-source catalog. Previous-version assets were retained.
The adjacent verification JSON records the final publication checkpoint.
