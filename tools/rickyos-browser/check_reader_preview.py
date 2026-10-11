#!/usr/bin/env python3
"""Native reader chrome/product-menu QA, including saved legacy toolbar settings."""
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
    parser.add_argument('--out', type=Path, default=Path('build/design-reader/screenshots'))
    args = parser.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ricky-reader-qa-') as directory:
        state = Path(directory)
        config = state / 'sd/.crosspoint'
        config.mkdir(parents=True)
        books = state / 'sd/books'
        books.mkdir()
        (books / '阅读与生活.txt').write_text(('这是阅读界面的隔离测试。保持正文的字体选择，让工具安静地陪伴阅读。\n\n' * 80))
        (config / 'settings.json').write_text(json.dumps({
            'uiTheme': 5, 'language': 'ZH_CN', 'onboardingVersion': 1,
            'orientation': 0, 'clockUtcOffsetQ': 32, 'clockAutoSync': 0,
            'sleepTimeoutMinutes': 60, 'showReaderMenu': 1, 'readerMenuStyle': 1,
            'textAntiAliasing': 1, 'statusBarChapterPageCount': 1,
            'statusBarBookProgressPercentage': 1, 'statusBarProgressBar': 0,
            'statusBarProgressBarThickness': 2, 'statusBarClock': 2,
            'statusBarBattery': 1, 'statusBarBatteryPercentage': 1,
        }))
        (config / 'user-guide.checked').write_text('1')
        preview = Preview(state, 18781)
        try:
            def shot(label, activity='EpubReader', size=(684,1216)):
                time.sleep(1.8)
                deadline = time.monotonic() + 10
                while not preview.get_frame() and time.monotonic() < deadline:
                    time.sleep(.1)
                frame = preview.get_frame()
                status = preview.status()
                # A busy reader can push its last Entering-activity line out
                # of the preview server's bounded log tail. Once the initial
                # reader is verified, an empty report does not imply exit.
                known = status['activity']
                if not frame or frame[1:3] != size or not status['alive'] or (known and known != activity):
                    raise RuntimeError(f'{label}: bad state {status}')
                colors = len(Image.open(io.BytesIO(frame[0])).getcolors(256) or ())
                (out / (label + '.png')).write_bytes(frame[0])
                print(f'{label}: {activity}, {size}, {colors} tones', flush=True)

            def tap(x,y):
                preview.input({'type':'touch','x1':x,'y1':y,'x2':x,'y2':y,'duration':120})

            def key(name):
                preview.input({'type':'key','key':name,'duration':120})

            def reopen(label, size=(684,1216)):
                # Product boot intentionally lands on Home, even with a saved
                # last book. Home remains portrait; orientation applies on open.
                shot(label + '-home', 'InxRecent')
                tap(.3,.94)
                shot(label + '-library', 'Library')
                tap(.18,.3)
                shot(label, size=size)

            shot('home', 'InxRecent')
            tap(.3,.94)
            shot('library', 'Library')
            tap(.18,.3)
            shot('reader')
            key('ENTER')
            shot('reader-menu-legacy-preference', 'EpubReaderMenu')
            tap(.61,.23)
            shot('text-settings', 'TextSettings')
            key('BACK')
            shot('menu-return', 'EpubReaderMenu')
            tap(.16,.23)
            shot('contents', 'EpubReaderChapterSelection')
            key('BACK')
            shot('contents-return', 'EpubReaderMenu')
            tap(.5,.78)
            shot('more', 'EpubReaderMenu')
            key('BACK')
            shot('more-return', 'EpubReaderMenu')
            key('BACK')
            shot('page-return')
            key('DOWN')
            shot('next-page')
            # Reuse only this disposable card. Stop before editing its settings;
            # The new reader viewport must be built from the saved orientation.
            for orientation in (1, 2, 3):
                preview.stop()
                settings = json.loads((config / 'settings.json').read_text())
                settings['orientation'] = orientation
                (config / 'settings.json').write_text(json.dumps(settings))
                preview.start()
                size = (684,1216) if orientation == 2 else (1216,684)
                reopen(f'reader-orientation-{orientation}', size)
                key('ENTER')
                shot(f'menu-orientation-{orientation}', 'EpubReaderMenu', size=size)
                key('BACK')
                shot(f'return-orientation-{orientation}', size=size)
            preview.stop()
            settings = json.loads((config / 'settings.json').read_text())
            settings.update(orientation=0, screenInverted=1)
            (config / 'settings.json').write_text(json.dumps(settings))
            preview.start()
            reopen('reader-night')
            key('ENTER')
            shot('menu-night', 'EpubReaderMenu')
            preview.stop()
            settings.update(screenInverted=0, statusBarTitle=2, statusBarClock=0,
                            statusBarChapterPageCount=0, statusBarBookProgressPercentage=0,
                            statusBarBattery=0, statusBarProgressBar=2)
            (config / 'settings.json').write_text(json.dumps(settings))
            preview.start()
            reopen('reader-immersive')
        finally:
            preview.close()


if __name__ == '__main__':
    main()
