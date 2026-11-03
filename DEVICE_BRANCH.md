# Device branch: ordinary Metalio E-Ink 4

This branch ports the application features of `codex/plus-1.2.11` (`2973acbb`)
to the ordinary ESP32-S3 / 800×480 SSD1677 device. Build from this repository root
after restoring the checked SDK overlay:

```sh
git submodule update --init freeink-sdk
python3 scripts/prepare_metalio_sdk.py
pio run -e rickyos_metalio_eink4
```

The SDK stays pinned to `96de1be6ce08eb732909e6e8149af8f892b9a2c5`.
`patches/metalio-eink4/freeink-sdk.patch` and its digest manifest preserve the
exact local SDK dependencies used by dev16, including the ordinary board's
display and peripheral work. The restore script verifies all changed paths,
is idempotent, and refuses a different SDK revision or conflicting local edits.
Use `python3 scripts/prepare_metalio_sdk.py --check` to verify an existing checkout.
The Plus source snapshot under `ports/`
is retained as verified input; its ESP32-S31 IDF project must not be flashed to
the ordinary device. See `docs/metalio-eink4-plus-features-20261029.md`.

Current tested version: `1.2.11-metalio-dev16`. The user accepted the native
layout and reported substantially reduced tab black flashes with a clean image
on 2026-11-03. Five changed UI updates are followed by a cleanup. This is scoped
tab appearance acceptance; camera, extended retention and the full hardware
regression remain incomplete. Build/host/runtime evidence and limitations are
recorded in `docs/metalio-eink4-ui-gray-20261029.md`.
