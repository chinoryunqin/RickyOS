#!/usr/bin/env python3
"""Isolated native-gray overlay/wake QA; no device or user's preview card is touched."""
import io
import json
from pathlib import Path
import tempfile
import time

from PIL import Image, ImageDraw, ImageChops
from server import Preview, seed_demo


def main():
    out = Path('build/transparent-wallpaper/screenshots')
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ricky-transparent-qa-') as directory:
        state = Path(directory)
        sd = state / 'sd'
        seed_demo(sd)
        config = sd / '.crosspoint/settings.json'
        settings = json.loads(config.read_text())
        settings.update(rickyStandbyFace=4, standbyShortcutEnabled=1, sleepScreenCoverMode=0,
                        clockAutoSync=0, rickyAutoOffIndex=0)
        config.write_text(json.dumps(settings, ensure_ascii=False))
        # Geometric test fixture, not a shipped wallpaper. Full-size alpha protects the page.
        sticker = Image.new('RGBA', (684, 1216))
        ImageDraw.Draw(sticker).rectangle((240, 400, 440, 700), fill=(0, 0, 0, 128))
        sticker.save(sd / 'standby.png')
        sticker.save(sd / 'images/chosen.png')
        preview = Preview(state, 18810)
        try:
            def shot(label, activity):
                time.sleep(1)
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    status, frame = preview.status(), preview.get_frame()
                    if status['activity'] == activity and status['input_ready'] and frame:
                        image = Image.open(io.BytesIO(frame[0])).convert('L')
                        if len(image.getcolors(256) or ()) > 2:
                            (out / (label + '.png')).write_bytes(frame[0])
                            return image
                    time.sleep(.1)
                raise AssertionError((label, status))

            def key(name):
                preview.input({'type': 'key', 'key': name, 'duration': 120})

            def tap(x, y):
                preview.input({'type': 'touch', 'x1': x, 'y1': y, 'x2': x, 'y2': y, 'duration': 120})

            for context in ('home', 'reader'):
                if context == 'home':
                    time.sleep(3)  # Firmware deliberately ignores power during its boot release barrier.
                if context == 'reader':
                    tap(.3, .94)
                    shot('library', 'Library')
                    tap(.18, .3)
                    time.sleep(1)
                activity = 'InxRecent' if context == 'home' else 'EpubReader'
                before = shot(context + '-before', activity)
                key('POWER')
                asleep = shot(context + '-standby', 'Standby')
                # Exclude only the PNG fixture. Standby must not add a status badge,
                # white backing, or any other change elsewhere (including the footer).
                difference = ImageChops.difference(before, asleep)
                mask = Image.new('L', before.size, 255)
                draw = ImageDraw.Draw(mask)
                draw.rectangle((225, 385, 455, 715), fill=0)
                assert ImageChops.multiply(difference, mask).getbbox() is None, context + ': page changed outside overlay'
                assert difference.crop((260, 420, 420, 680)).getbbox(), context + ': sticker missing'
                again = shot(context + '-standby-again', 'Standby')
                assert ImageChops.difference(asleep, again).getbbox() is None, 'alpha applied twice'
                key('POWER')
                awake = shot(context + '-awake', activity)
                assert ImageChops.difference(before.crop((0, 80, before.width, before.height)),
                                             awake.crop((0, 80, awake.width, awake.height))).getbbox() is None, 'sticker remained after wake'
                print(context + ': page gray retained, alpha blended once, wake restored', flush=True)
            key('BACK')
            shot('home-return', 'InxRecent')
            tap(.5, .94)
            shot('apps', 'AppsMenu')
            for _ in range(5):
                key('DOWN')
                time.sleep(.25)
            key('ENTER')
            shot('transparent-settings', 'RickyStandbySettings')
            tap(.4, .715)
            shot('transparent-picker', 'FileBrowser')
            key('ENTER')
            shot('transparent-image-preview', 'ImageViewer')
            key('ENTER')
            shot('transparent-installed', 'RickyStandbySettings')
            assert (sd / 'standby.png').read_bytes() == (sd / 'images/chosen.png').read_bytes()
            assert json.loads(config.read_text())['rickyStandbyFace'] == 4
            print('picker: PNG preview, install, persisted style and unchanged original alpha passed', flush=True)
        finally:
            preview.close()


if __name__ == '__main__':
    main()
