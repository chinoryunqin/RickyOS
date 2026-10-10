#!/usr/bin/env python3
"""Capture actual native 1.2 pages from an already-running isolated preview.

Uses its localhost input bridge only; never connects to a device or changes
settings. Ends on Home. Screenshots are build artifacts, not a second renderer.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import time
from urllib.request import Request, urlopen

from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8769")
    parser.add_argument("--out", type=Path, default=Path("build/design-stage2/screenshots"))
    parser.add_argument("--wait-seconds", type=float, default=2.2,
                        help="Capture delay; use 0.3 to check foreground gray without waiting for idle settle.")
    args = parser.parse_args()
    if not re.fullmatch(r"http://127\.0\.0\.1:\d+", args.url):
        parser.error("Use the isolated localhost preview URL, never a remote/device URL.")
    if not 0.1 <= args.wait_seconds <= 10:
        parser.error("Capture delay must be between 0.1 and 10 seconds.")
    page = urlopen(args.url, timeout=5).read().decode()
    token = re.search(r'name="ricky-token" content="([^"]+)"', page).group(1)
    args.out.mkdir(parents=True, exist_ok=True)

    def post(endpoint, payload):
        request = Request(args.url + endpoint, data=json.dumps(payload).encode(),
                          headers={"X-Ricky-Token": token, "Content-Type": "application/json"})
        return json.loads(urlopen(request, timeout=5).read())

    def tap(x, y):
        post("/api/input", {"type": "touch", "x1": x, "y1": y, "x2": x, "y2": y, "duration": 120})

    def capture(name, activity):
        time.sleep(args.wait_seconds)
        status = json.loads(urlopen(args.url + "/api/status", timeout=5).read())
        if not status["alive"] or status["activity"] != activity:
            raise RuntimeError(f"{name}: unexpected preview status {status}")
        data = urlopen(args.url + "/api/screenshot", timeout=5).read()
        image = Image.open(io.BytesIO(data))
        if image.size != (684, 1216):
            raise RuntimeError(f"{name}: wrong panel dimensions {image.size}")
        colors = len(image.convert("L").getcolors(256) or ())
        if colors <= 2:
            raise RuntimeError(f"{name}: native gray frame was not submitted")
        (args.out / (name + ".png")).write_bytes(data)
        entry = {"activity": activity, "size": list(image.size), "luminance_levels": colors,
                 "sha256": hashlib.sha256(data).hexdigest()}
        print(f"{name}: {activity}, 684 x 1216, {colors} levels")
        return entry

    post("/api/restart", {})
    # Boot/font loading is separate from the page's capture delay. Do not
    # misclassify a still-starting simulator as a slow/black-and-white menu.
    deadline = time.monotonic() + 10
    while True:
        status = json.loads(urlopen(args.url + "/api/status", timeout=5).read())
        if status["alive"] and status["input_ready"] and status["activity"] == "InxRecent":
            break
        if time.monotonic() >= deadline:
            raise RuntimeError(f"Preview did not finish booting: {status}")
        time.sleep(.05)
    result = {"home": capture("home", "InxRecent")}
    for name, x, activity in [("library", .30, "Library"), ("apps", .50, "AppsMenu"),
                             ("storage", .70, "RickyStorage"), ("settings", .90, "Settings")]:
        tap(x, .94)
        result[name] = capture(name, activity)
    tap(.10, .94)
    time.sleep(.4)
    tap(598 / 684, 122 / 1216)
    result["profile"] = capture("profile", "RickyProfile")
    post("/api/input", {"type": "key", "key": "BACK"})
    # The status endpoint reports the last Entering log, not stack resumption.
    # Routing through Library verifies that the popped Home tab bar works and
    # produces a fresh Entering log; no misleading current-activity claim.
    time.sleep(.4)
    tap(.30, .94)
    time.sleep(.4)
    tap(.10, .94)
    result["home-return"] = capture("home-return", "InxRecent")
    (args.out / "manifest.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")


if __name__ == "__main__":
    main()
