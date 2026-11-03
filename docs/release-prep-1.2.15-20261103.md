# 1.2.15 release preparation, 2026-11-03

Separate Pico/Plus candidates are based on the corresponding published 1.2.14
tags. The release carries shelf-folder persistence and complete folder choices,
numeric 80–400% line spacing, and MOBI/AZW3 covers. Pico also closes inherited
Wi-Fi on standby entry; Plus sets charge and input limits to the OEM 1.5A option
(1440mA). PDF and its resource-partition migration remain isolated experiments.

Both final-version application images passed checksum/hash, exact chip/board,
linked font-metadata round trips and the existing 512 KiB OTA guard. Pico's
local diagnostic script was removed; Plus POWER_CAPTURE, STANDBY_DIAG,
FIELD_DIAG and POWER_LOG were explicitly disabled and the final compile commands
were checked. Eight focused host scripts per target passed, including ASan/UBSan
production-method cases. The unchanged website passed 78 tests.

Pico's earlier trial established inherited-radio cleanup, actual light sleep
and short-key wake; the user confirmed folder choices and Kindle covers on dev3.
Those observations do not establish final-image acceptance of 400% spacing.
Plus's dev2 trial established the charger configuration and a charging-current
sample; it does not establish all reader interactions in the final image.

Final-image installation/startup and physical interaction acceptance are tracked
in `release-1.2.15-verification-20261103.json`. No public catalog is enabled until
that acceptance is resolved. No actual OTA installation test is claimed.
Public artifacts reuse the existing accepted boot/partition segments unchanged.
Raw device identities, full-flash backups and serial captures remain local.

Both final images were serial-installed, image-verified and booted normally. Flash outside each active app slot matched the complete pre-install backup. The maintainer confirmed both final candidates passed folder persistence after reboot, 256%/400% spacing including cancellation and page turns, MOBI/AZW3 covers, and short-power standby/wake. No new real OTA installation or overnight-current measurement is claimed.

Publication completed on the brand website, GitHub OTA mirror and COS. All six
public applications and the rolling offers/notes were read back and matched the
accepted images and single-source catalog. Previous-version assets were retained.
The adjacent verification JSON records the final publication checkpoint.
