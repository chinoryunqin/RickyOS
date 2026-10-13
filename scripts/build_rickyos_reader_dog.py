#!/usr/bin/env python3
"""RickyOS reading companion: the logo's dog peeking over the page edge -> 2-bpp Flash art.

The dog is not redrawn. It is cut out of the user-selected logo
(src/images/sources/rickyos/approved-logo.png, hash-checked), where it sits
behind the portrait: the portrait is erased, and the cheek it hid is rebuilt
on the head's own circle in the logo's 16 px stroke. Below the page edge only
the paws (same stroke, the mascot's two toe lines) and the tongue show.

Output: src/components/icons/rickyReaderDog.h, 2-bit gray (0 ink .. 3 paper)
plus a 1-bit coverage mask, so the dog hides the frame line it peeks over.
Developer-only: Pillow, numpy and scipy.
Run: python3 scripts/build_rickyos_reader_dog.py [--out header.h] [--preview dog.png]
"""
import argparse
import hashlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
from scipy.interpolate import PchipInterpolator

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/images/sources/rickyos/approved-logo.png"
SOURCE_SHA256 = "ac8c60ca236a864bb2736a2d015222aff2bc2bb667f3d7248c3d9f234863a12f"

CROP = (560, 340, 1080, 880)  # the dog's quarter of the logo
STROKE = 16                   # the logo's line weight at source size
HEAD = (224.0, 330.0, 176.0)  # head circle (cx, cy, r) in crop coordinates
EDGE = 438                    # page edge: just under the smile, the tongue hangs over it
SCALE = 0.22                  # 16 px stroke -> ~3.5 px on the panel


def isolate_dog(logo: np.ndarray) -> np.ndarray:
    labels, _ = ndimage.label(logo < 128)
    left, top, right, bottom = CROP
    region = labels[top:bottom, left:right]
    # The portrait and the dog share one connected outline; the mouth is its own piece.
    # The enclosing circle is the one piece taller than the dog's quarter.
    ring = [i + 1 for i, s in enumerate(ndimage.find_objects(labels)) if s[0].stop - s[0].start > 800]
    sizes = ndimage.sum(np.ones_like(region), region, range(1, labels.max() + 1))
    big = [i + 1 for i, s in enumerate(sizes) if s > 5000 and i + 1 not in ring]
    dog = np.isin(region, big)
    cx, cy, r = HEAD
    # Outer contour from the ear's left edge onto the head circle.
    contour = PchipInterpolator([150, 160, 200, 240, 280, 320, 360, 400, 440, 460],
                                [84, 82, 74, 64, 55.1, 48.1, 50.4, 62.4, 86.0, 104.0])
    for y in range(dog.shape[0]):
        if y < 150:  # the portrait's hair, left of the ear
            dog[y, :95 if y < 100 else int(round(95 - (y - 100) * 0.22))] = False
            continue
        if y >= 460:
            continue
        c = float(contour(y))
        for x in range(105):
            if x < c - 0.5:
                dog[y, x] = False
                continue
            patch_bottom = 315 + (x - 75) * (0.75 if x < 75 else 0.8)
            if y <= patch_bottom:
                dog[y, x] = True  # the eye patch reaches the rebuilt contour
            else:
                dog[y, x] = r - STROKE <= np.hypot(x - cx, y - cy) <= r + 0.5
    labels2, count = ndimage.label(dog)
    sizes = ndimage.sum(dog, labels2, range(1, count + 1))
    for i, s in enumerate(sizes):
        if s < 400:  # portrait specks
            dog[labels2 == i + 1] = False
    return dog


def compose(dog: np.ndarray):
    h, w = dog.shape
    yy, xx = np.mgrid[0:h, 0:w]
    cx, cy, r = HEAD
    silhouette = ndimage.binary_fill_holes(dog | (np.hypot(xx - cx, yy - cy) <= r))
    above = yy <= EDGE
    ink = np.full((h, w), 255, np.uint8)
    cover = np.zeros((h, w), np.uint8)
    cover[silhouette & above] = 255
    ink[dog & above] = 0
    tongue_box = (xx >= 175) & (xx <= 266) & (yy >= 405) & (yy <= 482)
    tongue = ndimage.binary_fill_holes(dog & tongue_box) & (yy > EDGE)
    cover[tongue] = 255
    ink[tongue & dog] = 0
    ink_img, cover_img = Image.fromarray(ink), Image.fromarray(cover)
    di, dc = ImageDraw.Draw(ink_img), ImageDraw.Draw(cover_img)
    for paw_x in (96, 358):  # under the cheeks, clear of the smile's ends
        pw, ph, rise = 106, 74, 46
        x0, x1, y0 = paw_x - pw // 2, paw_x + pw // 2, EDGE - rise
        y1 = y0 + ph
        radius = pw // 2 - 4
        dc.rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=255)
        di.rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=0)
        di.rounded_rectangle([x0 + STROKE, y0 + STROKE, x1 - STROKE, y1 - STROKE], radius=radius - STROKE, fill=255)
        for toe in (paw_x - pw // 6, paw_x + pw // 6):
            di.rounded_rectangle([toe - 5, y1 - 30, toe + 5, y1], radius=5, fill=0)
    ys, xs = np.nonzero(np.array(cover_img))
    box = (int(xs.min()) - 2, int(ys.min()) - 2, int(xs.max()) + 3, int(ys.max()) + 3)
    return ink_img.crop(box), cover_img.crop(box), EDGE - box[1]


def encode(ink: Image.Image, cover: Image.Image, edge: int):
    w, h = round(ink.width * SCALE), round(ink.height * SCALE)
    gray = np.array(ink.resize((w, h), Image.LANCZOS)).astype(int)
    alpha = np.array(cover.resize((w, h), Image.LANCZOS)) >= 128
    levels = np.clip((gray * 3 + 127) // 255, 0, 3)
    levels[~alpha] = 3
    return w, h, round(edge * SCALE), levels, alpha


def pack(values, bits):
    per = 8 // bits
    out = bytearray()
    for row in values:
        for i in range(0, len(row), per):
            byte = 0
            for j in range(per):
                v = int(row[i + j]) if i + j < len(row) else 0
                byte |= v << (8 - bits * (j + 1))
            out.append(byte)
    return bytes(out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=ROOT / "src/components/icons/rickyReaderDog.h")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    data = SOURCE.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise SystemExit("approved-logo.png changed; re-check the cut before regenerating.")
    logo = np.array(Image.open(SOURCE).convert("L"))
    w, h, edge, levels, alpha = encode(*compose(isolate_dog(logo)))
    gray_bytes, mask_bytes = pack(levels, 2), pack(alpha.astype(int), 1)
    lines = ["#pragma once", "", "#include <cstdint>", "",
             "// Generated by scripts/build_rickyos_reader_dog.py from sources/rickyos/approved-logo.png.",
             "// The logo's dog peeking over the page edge. Regenerate; never edit these bytes.",
             "// gray: 2 bpp, MSB-first, 0 = ink .. 3 = paper. mask: 1 bpp, MSB-first, 1 = covered.", "",
             "namespace RickyReaderDog {",
             f"inline constexpr int kWidth = {w};",
             f"inline constexpr int kHeight = {h};",
             "// Row of the page edge the dog peeks over: paws and tongue reach below it.",
             f"inline constexpr int kEdgeRow = {edge};",
             f"inline constexpr int kGrayStride = {(w + 3) // 4};",
             f"inline constexpr int kMaskStride = {(w + 7) // 8};", ""]
    for name, blob in (("kGray", gray_bytes), ("kMask", mask_bytes)):
        lines.append(f"inline constexpr uint8_t {name}[] = {{")
        for offset in range(0, len(blob), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in blob[offset:offset + 16]) + ",")
        lines += ["};", ""]
    lines += ["}  // namespace RickyReaderDog", ""]
    args.out.write_text("\n".join(lines))
    print(f"wrote {args.out} ({w}x{h}, edge row {edge}, {len(gray_bytes) + len(mask_bytes)} bytes)")
    if args.preview:
        page = np.full((h, w), 255, np.uint8)
        page[alpha] = (levels[alpha] * 85).astype(np.uint8)
        Image.fromarray(page).resize((w * 4, h * 4), Image.NEAREST).save(args.preview)


if __name__ == "__main__":
    main()
