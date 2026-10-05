#!/usr/bin/env python3
"""Build the RickyOS default standby wallpapers published at github.com/chinoryunqin/RickyOS-wallpapers.

Each source picture (any size, ideally 2:3 portrait) is center-cropped to the Read Pico's 9:16 screen,
resized to 684 x 1216, error-diffused to the panel's 16 gray levels and written as a 4-bit paletted
BMP: the reader copies it to /sleep.bmp unchanged and draws it in native 16-gray.

Output: wallpapers/*.bmp, wallpapers.json (GitHub release URLs) and mirror.json (jsDelivr), both in
the manifest format RickyWallpaperDownloadActivity reads, plus SHA256SUMS.

Run: python3 scripts/build_rickyos_wallpapers.py --sources DIR --output DIR [--tag v1.0.0]
"""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from PIL import Image

RELEASE_REPO = "chinoryunqin/RickyOS-wallpapers"
MANIFEST_VERSION = 1
WIDTH, HEIGHT = 684, 1216
LEVELS = 16
# (source stem, SD file name shown in the picker, published ASCII file name)
WALLPAPERS = [
    ("01-mountains", "云海", "01-mountains.bmp"),
    ("02-window", "窗边", "02-window.bmp"),
    ("03-cat", "猫与书", "03-cat.bmp"),
    ("04-moon", "月夜", "04-moon.bmp"),
    ("05-bamboo", "竹", "05-bamboo.bmp"),
    ("06-lamp", "夜灯", "06-lamp.bmp"),
]


def fit_to_screen(image: Image.Image) -> Image.Image:
    gray = image.convert("L")
    target = WIDTH / HEIGHT
    width, height = gray.size
    if width / height > target:
        crop = round(height * target)
        left = (width - crop) // 2
        gray = gray.crop((left, 0, left + crop, height))
    else:
        crop = round(width / target)
        top = (height - crop) // 2
        gray = gray.crop((0, top, width, top + crop))
    return gray.resize((WIDTH, HEIGHT), Image.LANCZOS)


def quantize(gray: Image.Image) -> Image.Image:
    palette = Image.new("P", (1, 1))
    values = []
    for level in range(LEVELS):
        values += [level * 17] * 3
    palette.putpalette(values + [0] * (768 - len(values)))
    return gray.convert("RGB").quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG)


def bmp4(indexed: Image.Image) -> bytes:
    row_bytes = (WIDTH * 4 + 31) // 32 * 4
    pixels = indexed.tobytes()
    rows = []
    for y in range(HEIGHT - 1, -1, -1):  # BMP rows run bottom-up
        line = pixels[y * WIDTH:(y + 1) * WIDTH]
        packed = bytearray((line[x] << 4) | (line[x + 1] if x + 1 < WIDTH else 0) for x in range(0, WIDTH, 2))
        rows.append(bytes(packed) + b"\0" * (row_bytes - len(packed)))
    palette = b"".join(bytes((level * 17, level * 17, level * 17, 0)) for level in range(LEVELS))
    data_offset = 14 + 40 + len(palette)
    image = b"".join(rows)
    header = struct.pack("<2sIHHI", b"BM", data_offset + len(image), 0, 0, data_offset)
    info = struct.pack("<IiiHHIIiiII", 40, WIDTH, HEIGHT, 1, 4, 0, len(image), 2835, 2835, LEVELS, LEVELS)
    return header + info + palette + image


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tag", default="v1.0.0")
    args = parser.parse_args()

    out = args.output / "wallpapers"
    out.mkdir(parents=True, exist_ok=True)
    items, sums = [], []
    for stem, name, file in WALLPAPERS:
        data = bmp4(quantize(fit_to_screen(Image.open(args.sources / f"{stem}.png"))))
        (out / file).write_bytes(data)
        items.append({"name": name, "file": file, "size": len(data), "crc32": zlib.crc32(data) & 0xFFFFFFFF})
        sums.append(f"{hashlib.sha256(data).hexdigest()}  wallpapers/{file}")
        print(f"{name}: {file} {len(data):,} B")

    github = f"https://github.com/{RELEASE_REPO}/releases/download/{args.tag}/"
    jsdelivr = f"https://cdn.jsdelivr.net/gh/{RELEASE_REPO}@{args.tag}/wallpapers/"
    fastly = f"https://fastly.jsdelivr.net/gh/{RELEASE_REPO}@{args.tag}/wallpapers/"
    for manifest, base, mirrors in (("wallpapers.json", github, [jsdelivr, fastly]),
                                    ("mirror.json", jsdelivr, [fastly, github])):
        document = {"version": MANIFEST_VERSION, "baseUrl": base, "mirrors": mirrors, "items": items}
        (args.output / manifest).write_text(json.dumps(document, ensure_ascii=False, indent=1) + "\n")
    (args.output / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    print(f"Ready: {args.output}")


if __name__ == "__main__":
    main()
