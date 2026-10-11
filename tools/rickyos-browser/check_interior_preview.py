#!/usr/bin/env python3
"""Exercise pushed UI using an isolated simulator/card, never the user's preview.

Screenshots are ignored QA artifacts. The temporary card is discarded on exit;
there is no device I/O or network service started by this check.
"""
import io
import argparse
import json
from pathlib import Path
import tempfile
import time

from PIL import Image
from server import Preview


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/design-interior/screenshots'))
    parser.add_argument('--check-home', action='store_true')
    args = parser.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ricky-interior-qa-") as directory:
        state = Path(directory)
        sd = state / "sd"
        config = sd / ".crosspoint"
        config.mkdir(parents=True)
        books = sd / "books"
        books.mkdir()
        name = "一本名字非常长但仍然需要保留扩展名的测试书籍.txt"
        (books / name).write_text("这是隔离模拟器的排版测试书籍，不会修改用户的文件。\n" * 100)
        (books / "阅读与生活.txt").write_text("读几页，歇一会儿。\n" * 100)
        (books / "文件夹").mkdir()
        for i in range(128):
            (books / "文件夹" / f"file-{i}.bin").write_bytes(b"test")
        (books / "文件夹" / ".hidden.bin").write_bytes(b"hidden")
        (books / ("长名字需要中间省略" * 6 + "（第三版）.txt")).write_text("长文件名测试\n" * 50)
        (config / "settings.json").write_text(json.dumps({
            "uiTheme": 5, "inxTabPosition": 1, "language": "ZH_CN", "onboardingVersion": 1,
            "orientation": 0, "rickyNickname": "Ricky", "clockUtcOffsetQ": 32,
            "clockAutoSync": 0, "sleepTimeoutMinutes": 60, "showReaderMenu": 1,
            "readerMenuStyle": 1, "textAntiAliasing": 1,
        }, ensure_ascii=False))
        (config / "user-guide.checked").write_text("1")
        preview = Preview(state, 18780)
        try:
            def shot(label, activity=None, gray=True, size=(684, 1216)):
                time.sleep(1.8)
                deadline = time.monotonic() + 8
                while (not preview.status()["input_ready"] or not preview.get_frame()) and time.monotonic() < deadline:
                    time.sleep(.1)
                frame = preview.get_frame()
                status = preview.status()
                if not frame or frame[1:3] != size or not status["alive"]:
                    raise RuntimeError(f"{label}: simulator/frame failed: {status}")
                if activity and status["activity"] != activity:
                    raise RuntimeError(f"{label}: expected {activity}, got {status}")
                colors = len(Image.open(io.BytesIO(frame[0])).getcolors(256) or ())
                # The input bridge can be ready while the last boot splash is
                # still published. Wait for the first foreground gray frame.
                while gray and colors <= 2 and time.monotonic() < deadline:
                    time.sleep(.1)
                    frame = preview.get_frame()
                    colors = len(Image.open(io.BytesIO(frame[0])).getcolors(256) or ())
                if gray and colors <= 2:
                    raise RuntimeError(f"{label}: native-gray UI missing ({colors} tones)")
                (out / (label + ".png")).write_bytes(frame[0])
                print(f"{label}: {status['activity']}, {colors} tones", flush=True)

            def tap(x, y):
                preview.input({"type": "touch", "x1": x, "y1": y, "x2": x, "y2": y, "duration": 120})

            def back():
                preview.input({"type": "key", "key": "BACK", "duration": 120})
                time.sleep(.6)

            shot("home")
            tap(.9, .94)
            shot("settings", "Settings")
            tap(.27, .31)
            shot("display-settings", "Settings")
            back()
            tap(.73, .31)
            shot("reading-settings", "Settings")
            back()
            tap(.73, .44)
            shot("fonts-settings", "FontLibrary")
            back()
            tap(.7, .94)
            shot("storage", "RickyStorage")
            tap(.5, .58)
            shot("files-root", "FileBrowser")
            tap(.3, .24)
            shot("files-books", "FileBrowser")
            # Open the known book directly from the shelf; reader menus share
            # their callbacks with TXT and EPUB without changing action IDs.
            back()
            back()
            tap(.3, .94)
            shot("library", "Library")
            tap(.18, .3)
            shot("reader", "EpubReader", gray=False)
            preview.input({"type": "key", "key": "ENTER", "duration": 120})
            shot("reader-menu", "EpubReaderMenu")
            tap(.5, .78)
            shot("reader-more", "EpubReaderMenu")
            back()
            shot("reader-more-back", "EpubReaderMenu")
            tap(.5, .44)
            shot("reader-orientation-popup", "EpubReaderMenu")
            back()
            shot("reader-popup-back", "EpubReaderMenu")
            tap(.61, .23)
            shot("text-settings", "TextSettings")
            back()
            shot("text-settings-back", "EpubReaderMenu")
            tap(.5, .44)
            shot("reader-orientation-before-rotate", "EpubReaderMenu")
            tap(.5, .49)
            shot("reader-menu-landscape", "EpubReaderMenu", size=(1216, 684))
            if args.check_home:
                tap(.92, .14)
                shot("reader-visible-home", "InxRecent")
                tap(.9, .94)
                shot("settings-help-entry", "Settings")
                tap(.5, .81)
                shot("help", "RickyHelp")
                tap(.5, .15)
                shot("help-reading", "RickyHelp")
                if (out / "help-reading.png").read_bytes() == (out / "help.png").read_bytes():
                    raise RuntimeError("help-reading: detail did not open")
                back()
                shot("help-return")
                tap(.5, .51)
                shot("help-website-qr", "QrDisplay", gray=False)
                back()
                back()
                shot("settings-help-return")
                tap(.5, .94)
                shot("apps", "AppsMenu")
                preview.input({"type": "key", "key": "DOWN", "duration": 120})
                shot("apps-selected", "AppsMenu")
                preview.input({"type": "key", "key": "DOWN", "duration": 120})
                shot("apps-selected-next", "AppsMenu")
        finally:
            preview.close()


if __name__ == "__main__":
    main()
