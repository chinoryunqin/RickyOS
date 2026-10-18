#!/usr/bin/env python3
"""Flag A4 preview screens that draw under the case.

The EEGO A4's case window, measured on a device (2026-10-18, a full-screen ruler
photographed head-on), hides about 6 px at the top and bottom, 7 px on the left and
10 px on the right of the 552x768 portrait panel, and its corners are rounded with a
~22 px radius. Anything drawn there is invisible. This script masks that region (plus
a comfort margin so content never touches the bezel) and reports every preview PNG
with ink inside it.

Run: python3 tools/rickyos-browser/check_eego_window.py build/eego-preview [--margin 4] [--marked DIR]
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

# Hidden edge (px) for the portrait panel: top, right, bottom, left; and the window radius.
HIDDEN = (6, 10, 6, 7)
RADIUS = 22
INK = 160  # darker than this counts as drawn content
# Screens allowed to reach the bezel: the control-center sheet's bottom edge spans the
# whole window by design (a sheet pulled down from the top).
ALLOWED = ("control-center",)


def window_mask(size, margin):
    """255 where content may be drawn, 0 under the case or within the margin of it."""
    w, h = size
    portrait = h >= w
    top, right, bottom, left = HIDDEN
    if not portrait:  # landscape: the portrait panel turned clockwise
        top, right, bottom, left = left, top, right, bottom
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (left + margin, top + margin, w - 1 - right - margin, h - 1 - bottom - margin),
        radius=max(0, RADIUS - margin), fill=255)
    return mask


def check(path, margin):
    img = Image.open(path).convert("L")
    mask = window_mask(img.size, margin)
    ink = img.point(lambda v: 255 if v < INK else 0)
    outside = Image.composite(Image.new("L", img.size, 0), ink, mask)
    box = outside.getbbox()
    count = outside.histogram()[255]
    return count, box, img, mask


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("preview_dir", type=Path)
    parser.add_argument("--margin", type=int, default=4)
    parser.add_argument("--marked", type=Path, help="write each failing screen with the hidden area shaded")
    args = parser.parse_args()
    failures = []
    files = sorted(args.preview_dir.glob("*.png"))
    for path in files:
        if any(name in path.name for name in ALLOWED):
            continue
        count, box, img, mask = check(path, args.margin)
        if count:
            failures.append((path.name, count, box))
            if args.marked:
                args.marked.mkdir(parents=True, exist_ok=True)
                shade = Image.new("RGB", img.size, (255, 120, 120))
                out = Image.composite(img.convert("RGB"), shade, mask)
                out.save(args.marked / path.name)
    for name, count, box in failures:
        print(f"OUTSIDE {name}: {count} px, bbox {box}")
    print(f"{len(files) - len(failures)}/{len(files)} screens inside the A4 window")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
