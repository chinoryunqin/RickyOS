#!/usr/bin/env bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$TASK_ROOT"
mkdir -p .local
clang++ -std=c++20 -Wall -Wextra -Werror tests/test_plus_framebuffer.cpp -o .local/test_plus_framebuffer
.local/test_plus_framebuffer
clang -Ifirmware/rickyos-plus-1.2/upstream/freeink-sdk/libs/font/FreeInkFont/include -c firmware/rickyos-plus-1.2/upstream/freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c -o .local/plus-font-alloc.o
clang -c firmware/rickyos-plus-1.2/upstream/lib/uzlib/src/tinflate.c -o .local/plus-font-inflate.o
clang++ -std=c++20 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wl,-dead_strip \
  -Itests/font-host-stubs \
  -Ifirmware/rickyos-plus-1.2/upstream/lib/InflateReader \
  -Ifirmware/rickyos-plus-1.2/upstream/lib/Utf8 \
  -Ifirmware/rickyos-plus-1.2/upstream/freeink-sdk/libs/font/FreeInkFont/include \
  -Ifirmware/rickyos-plus-1.2/upstream/lib/EpdFont \
  -Ifirmware/rickyos-plus-1.2/generated \
  -Ifirmware/rickyos-plus-1.2/upstream/lib/uzlib/src \
  tests/test_plus_fonts.cpp \
  firmware/rickyos-plus-1.2/upstream/lib/EpdFont/FontDecompressor.cpp \
  firmware/rickyos-plus-1.2/upstream/lib/InflateReader/InflateReader.cpp \
  firmware/rickyos-plus-1.2/upstream/lib/Utf8/Utf8.cpp \
  .local/plus-font-alloc.o .local/plus-font-inflate.o -o .local/test_plus_fonts
.local/test_plus_fonts

python3 tests/test_plus_clock_owner.py
python3 tests/test_plus_clock_radio.py

clang++ -std=c++17 -Wall -Wextra -Werror tests/test_plus_http_date.cpp -o .local/test_plus_http_date
.local/test_plus_http_date

# Plus peripherals, shared input, display and recorder regression suites.
python3 tests/test_plus_audio.py
python3 tests/test_plus_frontlight_panel.py
python3 tests/test_plus_standby_frontlight.py
python3 tests/test_plus_button_mapping.py
python3 tests/test_plus_button_settings.py
python3 tests/test_plus_control_center_layout.py
python3 tests/test_plus_haptic.py
python3 tests/test_plus_haptic_input.py
python3 tests/test_plus_recorder.py
python3 tests/test_plus_recorder_ui.py
python3 tests/test_plus_regional_gray.py
python3 tests/test_plus_regional_raster.py
python3 tests/test_plus_transparent_wallpaper.py
python3 tests/test_plus_usb.py
python3 tests/test_plus_usb_remount.py
python3 tests/test_plus_download_cancel.py
