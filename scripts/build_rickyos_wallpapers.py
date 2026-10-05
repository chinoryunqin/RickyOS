#!/usr/bin/env python3
"""Build the RickyOS default standby wallpapers published at github.com/chinoryunqin/RickyOS-wallpapers.

Each source picture (any size, ideally 2:3 portrait) is center-cropped to the Read Pico's 9:16 screen,
resized to 684 x 1216, error-diffused to the panel's 16 gray levels and written as a 4-bit paletted
BMP: the reader copies it to /sleep.bmp unchanged and draws it in native 16-gray.

Output: wallpapers/*.bmp, wallpapers.json (GitHub release URLs) and mirror.json (jsDelivr), both in
the manifest format RickyWallpaperDownloadActivity reads, plus SHA256SUMS.

Run: python3 scripts/build_rickyos_wallpapers.py --sources DIR --output DIR --slogan-font SmileySans-Oblique.otf [--tag v1.1.0]
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
# (source stem, SD file name shown in the picker, published ASCII file name[, slogan lines])
# Light pictures only: large black areas look heavy on e-paper. Slogans are set with
# --slogan-font (Smiley Sans) under the doodle, never by the image model.
WALLPAPERS = [
    # Logo style first: the first picture becomes the standby picture when none is set.
    ("31-reading-together", "一起读书", "31-reading-together.bmp"),
    ("32-dog-nap", "小狗午睡", "32-dog-nap.bmp"),
    ("33-moon-reading", "月亮上读书", "33-moon-reading.bmp"),
    ("34-dog-fetch", "叼书小狗", "34-dog-fetch.bmp"),
    ("35-book-nap", "书本盖脸", "35-book-nap.bmp"),
    ("36-peek", "书后探头", "36-peek.bmp"),
    ("21-one-more-page", "先看一页", "21-one-more-page.bmp", ("先看一页", "再看亿页")),
    ("22-do-not-disturb", "别催", "22-do-not-disturb.bmp", ("别催", "在看书")),
    ("23-battery-full", "电量充足", "23-battery-full.bmp", ("电量充足", "精神不足")),
    ("24-slow-reading", "慢慢读", "24-slow-reading.bmp", ("慢慢读", "不着急")),
    ("25-stay-home", "不出门", "25-stay-home.bmp", ("只要不出门", "就是好天气")),
    ("26-fish-culture", "摸鱼", "26-fish-culture.bmp", ("摸鱼", "也要有文化")),
    ("12-bear-balloon", "小熊的气球", "12-bear-balloon.bmp"),
    ("13-whale-clouds", "云上鲸鱼", "13-whale-clouds.bmp"),
    ("14-hedgehog-library", "刺猬去图书馆", "14-hedgehog-library.bmp"),
    ("15-fox-tree", "树上的狐狸", "15-fox-tree.bmp"),
]
# Logo-style line art is fitted into the upper band so it never meets the time corner.
LOGO_STYLE = {"31-reading-together", "32-dog-nap", "33-moon-reading", "34-dog-fetch", "35-book-nap", "36-peek"}
LOGO_BAND = (0.16, 0.62)
# Where to center the 9:16 crop horizontally (0 = left edge, 0.5 = middle).
CROP_ANCHOR = {"12-bear-balloon": 0.0}
SLOGAN_SIZE = 76
SLOGAN_BOTTOM = 0.72  # keep the lower band free for Standby's time corner

def fit_to_screen(image: Image.Image, anchor: float = 0.5) -> Image.Image:
    if image.mode in ("RGBA", "LA", "P"):
        # Transparent sources: their hidden pixels are black, so flatten onto paper.
        paper = Image.new("RGBA", image.size, (255, 255, 255, 255))
        paper.alpha_composite(image.convert("RGBA"))
        image = paper
    gray = image.convert("L")
    target = WIDTH / HEIGHT
    width, height = gray.size
    if width / height > target:
        crop = round(height * target)
        left = round((width - crop) * anchor)
        gray = gray.crop((left, 0, left + crop, height))
    else:
        crop = round(width / target)
        top = (height - crop) // 2
        gray = gray.crop((0, top, width, top + crop))
    return gray.resize((WIDTH, HEIGHT), Image.LANCZOS)


def fit_to_band(gray: Image.Image) -> Image.Image:
    """Shrink a line drawing that runs outside LOGO_BAND and center it in the band."""
    from PIL import ImageOps
    box = ImageOps.invert(gray).point(lambda v: 255 if v > 60 else 0).getbbox()
    if not box:
        return gray
    top, bottom = round(HEIGHT * LOGO_BAND[0]), round(HEIGHT * LOGO_BAND[1])
    margin = round(WIDTH * 0.12)
    art = gray.crop(box)
    scale = min(1.0, (bottom - top) / art.height, (WIDTH - 2 * margin) / art.width)
    if scale < 1.0:
        art = art.resize((round(art.width * scale), round(art.height * scale)), Image.LANCZOS)
    elif box[1] >= top and box[3] <= bottom:
        return gray  # already inside the band
    page = Image.new("L", (WIDTH, HEIGHT), 255)
    page.paste(art, ((WIDTH - art.width) // 2, top + (bottom - top - art.height) // 2))
    return page


def add_slogan(gray: Image.Image, lines, font_path: Path) -> Image.Image:
    """Set the slogan centered under the doodle, ending above the time-corner band."""
    from PIL import ImageDraw, ImageFont, ImageOps
    font = ImageFont.truetype(str(font_path), SLOGAN_SIZE)
    ink = ImageOps.invert(gray).point(lambda v: 255 if v > 60 else 0).getbbox()
    drawing_bottom = ink[3] if ink else int(HEIGHT * 0.5)
    line_height = round(SLOGAN_SIZE * 1.3)
    block = line_height * len(lines)
    top = min(drawing_bottom + round(SLOGAN_SIZE * 0.9), round(HEIGHT * SLOGAN_BOTTOM) - block)
    canvas = gray.copy()
    draw = ImageDraw.Draw(canvas)
    for index, line in enumerate(lines):
        width = draw.textlength(line, font=font)
        draw.text(((WIDTH - width) / 2, top + index * line_height), line, font=font, fill=0)
    return canvas


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
    parser.add_argument("--tag", default="v1.1.0")
    parser.add_argument("--slogan-font", type=Path, help="Smiley Sans (OFL) for the slogan pictures")
    args = parser.parse_args()

    out = args.output / "wallpapers"
    out.mkdir(parents=True, exist_ok=True)
    items, sums = [], []
    for stem, name, file, *slogan in WALLPAPERS:
        gray = fit_to_screen(Image.open(args.sources / f"{stem}.png"), CROP_ANCHOR.get(stem, 0.5))
        if stem in LOGO_STYLE:
            gray = fit_to_band(gray)
        if slogan:
            if not args.slogan_font:
                parser.error("--slogan-font is required for the slogan pictures")
            gray = add_slogan(gray, slogan[0], args.slogan_font)
        data = bmp4(quantize(gray))
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
