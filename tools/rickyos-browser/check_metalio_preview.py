#!/usr/bin/env python3
"""Exercise the actual ordinary-panel firmware on disposable QA cards.

Captures are native 480x800 pixels with the driver's four-tone quantization.
This checks host rendering/routing, never panel optics or physical touch.
"""
import argparse
import io
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import time

from PIL import Image
from server import Preview, ROOT


def seed(sd, nickname, titles):
    config = sd / '.crosspoint'
    config.mkdir(parents=True)
    books = sd / 'books'
    books.mkdir()
    recent = []
    for title in titles:
        path = '/books/' + title + '.txt'
        (sd / path[1:]).write_text('给阅读留一点时间。这一页检查原生字号和翻页。\n\n' * 80)
        recent.append(dict(path=path, title=title, author='测试作者', coverBmpPath=''))
    (config / 'settings.json').write_text(json.dumps(dict(
        uiTheme=5, inxTabPosition=1, language='ZH_CN', onboardingVersion=1,
        orientation=0, rickyNickname=nickname, clockAutoSync=0,
        clockUtcOffsetQ=32, sleepTimeoutMinutes=60, libraryUseMetadata=0), ensure_ascii=False))
    (config / 'recent.json').write_text(json.dumps(dict(books=recent), ensure_ascii=False))
    (config / 'user-guide.checked').write_text('1')
    # Production source manifest; the real Library builder creates its own index.
    data = b'LSR1' + struct.pack('<H', 1) + bytes([2, 6]) + b'/books'
    checksum = 2166136261
    for byte in data:
        checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
    (config / 'library-sources.v1').write_bytes(data + struct.pack('<I', checksum))
    (config / 'library-incoming.txt').write_text('*\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build-root', type=Path, default=ROOT / '.pio/build')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cases = [('empty', 'Ricky', []),
             ('long', '阅读生活阅读生活阅读生活', ['为什么伟大不能被计划：长书名原生排版检查']),
             ('six', 'Ricky', ['瓦尔登湖', '小王子', '浮生六记', '人间词话', '诗经',
                               '为什么伟大不能被计划：长书名原生排版检查'])]
    results = {}
    binary_hash = hashlib.sha256((args.build_root / "simulator_rickyos_metalio_eink4/program").read_bytes()).hexdigest()
    for case, nickname, titles in cases:
        with tempfile.TemporaryDirectory(prefix='metalio-ui-qa-') as directory:
            state = Path(directory)
            seed(state / 'sd', nickname, titles)
            preview = Preview(state, 18951, 'simulator_rickyos_metalio_eink4', args.build_root)
            try:
                def tap(x, y):
                    preview.input(dict(type='touch', x1=x, y1=y, x2=x, y2=y, duration=120))

                def shot(label, activity):
                    time.sleep(2)
                    status = preview.status()
                    frame = preview.get_frame()
                    if not frame or frame[1:3] != (480, 800) or not status['alive'] or status['activity'] != activity:
                        raise RuntimeError(f'{label}: {status}, frame={frame[1:3] if frame else None}')
                    colors = Image.open(io.BytesIO(frame[0])).getcolors(256)
                    levels = sorted(color for _, color in colors)
                    if levels != [0, 85, 170, 255]:
                        raise RuntimeError(f'{label}: unexpected committed tones {levels}')
                    (args.out / (label + '.png')).write_bytes(frame[0])
                    results[label] = dict(activity=activity, size=[480, 800], levels=levels,
                                          sha256=hashlib.sha256(frame[0]).hexdigest(), binary_sha256=binary_hash)
                    print(label, activity, '480x800, four tones', flush=True)

                time.sleep(1)
                shot(case + '-home', 'InxRecent')
                tap(.875, .10)
                shot(case + '-badges', 'RickyBadges')
                preview.input(dict(type='key', key='BACK', duration=120))
                time.sleep(.4)
                tap(.3, .94)
                shot(case + '-library', 'Library')
                if case == 'six':
                    for label, x, activity in [('apps', .5, 'AppsMenu'), ('storage', .7, 'RickyStorage'),
                                               ('settings', .9, 'Settings')]:
                        tap(x, .94)
                        shot(label, activity)
            finally:
                preview.close()
                (args.out / (case + '-simulator.log')).write_bytes((state / 'simulator.log').read_bytes())
    (args.out / 'checks.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
