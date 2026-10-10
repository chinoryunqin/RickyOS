#!/usr/bin/env python3
"""Smoke-test empty/long-nickname/six-book states with disposable fake SD cards.

No user preview/card/settings are modified. Writes only ignored QA screenshots.
"""
import io
import json
from pathlib import Path
import tempfile
import time

from PIL import Image
from server import Preview


def main():
    out = Path("build/design-stage2/screenshots")
    out.mkdir(parents=True, exist_ok=True)
    for name, nickname, titles in [
        ("empty", "Ricky", []),
        ("long-nickname", "阅读生活阅读生活阅读生活阅读生活", ["一个书名特别长的测试样本"]),
        ("six-books", "Ricky", ["瓦尔登湖", "小王子", "浮生六记", "人间词话", "诗经", "阅读与生活"]),
    ]:
        with tempfile.TemporaryDirectory(prefix="ricky-design-qa-") as directory:
            state = Path(directory)
            sd = state / "sd"
            config = sd / ".crosspoint"
            config.mkdir(parents=True)
            books_dir = sd / "books"
            books_dir.mkdir()
            recent = []
            for title in titles:
                (books_dir / (title + ".txt")).write_text("这是隔离模拟器里的排版测试书籍。\n" * 40)
                recent.append({"path": "/books/" + title + ".txt", "title": title,
                               "author": "测试作者", "coverBmpPath": ""})
            (config / "settings.json").write_text(json.dumps(
                {"uiTheme": 5, "inxTabPosition": 1, "language": "ZH_CN", "onboardingVersion": 1,
                 "orientation": 0, "rickyNickname": nickname, "clockUtcOffsetQ": 32,
                 "clockAutoSync": 0, "sleepTimeoutMinutes": 60}, ensure_ascii=False))
            (config / "recent.json").write_text(json.dumps({"books": recent}, ensure_ascii=False))
            (config / "user-guide.checked").write_text("1")
            # This creates an isolated SDL process/bridge, not another browser tab.
            preview = Preview(state, 18779)
            try:
                def shot(label):
                    time.sleep(2.3)
                    frame = preview.get_frame()
                    if not frame or frame[1:3] != (684, 1216) or not preview.status()["alive"]:
                        raise RuntimeError(f"{label}: simulator/frame failed")
                    image = Image.open(io.BytesIO(frame[0]))
                    if len(image.getcolors(256) or ()) <= 2:
                        raise RuntimeError(f"{label}: gray resting frame absent")
                    (out / (label + ".png")).write_bytes(frame[0])
                    print(f"{label}: native 684 x 1216 frame captured")
                shot(name + "-home")
                if name == "six-books":
                    preview.input({"type": "touch", "x1": .30, "y1": .94, "x2": .30, "y2": .94, "duration": 120})
                    shot(name + "-library")
                    for filter_name, x in [("reading", .31), ("unread", .49), ("all", .14)]:
                        preview.input({"type": "touch", "x1": x, "y1": .17, "x2": x, "y2": .17, "duration": 120})
                        shot(name + "-" + filter_name)
            finally:
                preview.close()


if __name__ == "__main__":
    main()
