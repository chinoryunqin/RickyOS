#!/usr/bin/env python3
"""Native small-panel typography, using the same pinned Noto sources as 1.2.

Generate glyphs at their actual output size with four coverage levels, matching
the ordinary driver output. No page or text bitmap resampling.
"""
from pathlib import Path
import argparse
import hashlib
import subprocess
import sys

from build_rickyos_design_fonts import PINNED, ROOT, SOURCES, FONTS, ui_characters

FACES = (
    ("metalio_body_10_regular", 10, "Regular", True, 28, 22),
    ("metalio_book_10_medium", 10, "Medium", True, 28, 22),
    ("metalio_caption_8_regular", 8, "Regular", False, 24, 19),
    ("metalio_headline_12_medium", 12, "Medium", False, 32, 26),
    ("metalio_title_14_bold", 14, "Bold", False, 40, 31),
    ("metalio_display_19_bold", 19, "Bold", False, 48, 40),
)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--face', action='append', choices=[face[0] for face in FACES])
    args = parser.parse_args()
    for weight, expected in PINNED.items():
        assert hashlib.sha256((SOURCES / f"NotoSansSC-{weight}.otf").read_bytes()).hexdigest() == expected
    ui = ui_characters()
    common = (ROOT / "lib/EpdFont/scripts/cn_rickyos_chars.txt").read_text()
    full = "".join(sorted(set(ui + common)))
    for name, size, weight, all_chars, height, ascender in FACES:
        if args.face and name not in args.face:
            continue
        chars = "0123456789月日.:%％/- " if "display" in name else full if all_chars else ui
        command = [sys.executable, "lib/EpdFont/scripts/fontconvert.py", name, str(size),
                   str(SOURCES / f"NotoSansSC-{weight}.otf"), "--2bit", "--characters", chars,
                   "--line-height", str(height), "--ascender", str(ascender)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, check=True)
        header = b"\n".join(line.rstrip() for line in result.stdout.splitlines()) + b"\n"
        (FONTS / f"{name}.h").write_bytes(header)
        print(f"{name}: {len(chars)} characters, {len(header)} bytes", flush=True)

if __name__ == "__main__":
    main()
