#!/usr/bin/env python3
"""Verify A4 navigation using disposable SD cards and real native frames; no device I/O."""
import argparse
import json
from pathlib import Path
import tempfile
import time

from server import Preview


def run_case(out, orientation, label, scenario='navigation'):
    with tempfile.TemporaryDirectory(prefix='eego-qa-') as directory:
        state = Path(directory)
        config = state / 'sd/.crosspoint'
        config.mkdir(parents=True)
        books = state / 'sd/books'
        books.mkdir()
        titles = ['CrossMux用户手册', '随身书房', '瓦尔登湖', '小王子', '人间词话', '第六本：一个特别长的测试书名']
        if scenario == 'empty':
            titles = []
        elif scenario == 'long':
            titles[0] = '一个特别长的中文书名：在有限的屏幕上保持清晰的阅读和导航'
        recent = []
        for title in titles:
            (books / (title + '.txt')).write_text('这是A4隔离模拟器中的测试书籍。阅读、翻页与字号调整。\n' * 100)
            recent.append({'path': '/books/' + title + '.txt', 'title': title,
                           'author': 'Ricky AI Studio', 'coverBmpPath': ''})
        (config / 'recent.json').write_text(json.dumps({'books': recent}, ensure_ascii=False))
        (config / 'settings.json').write_text(json.dumps({
            'uiTheme': 5, 'inxTabPosition': 1, 'language': 'ZH_CN', 'onboardingVersion': 1,
            'orientation': orientation, 'rickyNickname': '名字特别长的阅读爱好者' if scenario == 'long' else 'Ricky',
            'clockUtcOffsetQ': 32, 'clockTimezone': 255, 'clockAutoSync': 0,
            'sleepTimeoutMinutes': 60, 'tapForReaderMenu': 1, 'readerMenuStyle': 0,
        }, ensure_ascii=False))
        (config / 'user-guide.checked').write_text('1')
        preview = Preview(state, 18786 + orientation, 'simulator_rickyos_eego_a4', orientation)
        try:
            view_orientation = orientation

            def shot(name, expected):
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    time.sleep(.1)
                    status, frame = preview.status(), preview.get_frame()
                    if frame and status['alive'] and status['input_ready'] and status['activity'] == expected:
                        break
                else:
                    raise RuntimeError((name, preview.status()))
                time.sleep(.5)
                frame = preview.get_frame()
                size = (552, 768) if view_orientation == 0 else (768, 552)
                assert frame[1:3] == size, (name, frame[1:3])
                (out / (label + '-' + name + '.png')).write_bytes(frame[0])
                print(label, name, status['activity'], frame[1:3], flush=True)

            def tap(x, y):
                preview.input({'type': 'touch', 'x1': x, 'y1': y, 'x2': x, 'y2': y, 'duration': 120})

            def back():
                preview.input({'type': 'key', 'key': 'POWER', 'duration': 120})

            shot('home', 'InxRecent')
            time.sleep(2)  # clear the boot power-key guard
            if scenario == 'navigation':
                preview.input({'type': 'touch', 'x1': .5, 'y1': .01, 'x2': .5, 'y2': .45, 'duration': 300})
                shot('control-center', 'FrontlightPanel')
                back()
                shot('home-from-control-center', 'InxRecent')
            tap(.9, .94)
            shot('settings', 'Settings')
            if scenario != 'navigation':
                return
            root = preview.get_frame()[3]
            for i, name in enumerate(['display', 'reading', 'network', 'fonts', 'files', 'system']):
                if orientation == 0:
                    x, y = (.27 if i % 2 == 0 else .73), (.32 + .156 * (i // 2))
                else:
                    x, y = (.18 + .32 * (i % 3)), (.435 + .165 * (i // 3))
                tap(x, y)
                shot(name + '-settings', 'FontLibrary' if name == 'fonts' else 'Settings')
                if preview.get_frame()[3] == root:
                    raise RuntimeError(name + ' category did not open')
                if name == 'system':
                    tap(.3, .25 if orientation == 0 else .35)
                    shot('a4-keys-help', 'RickyOptionList')
                    tap(.4, .14 if orientation == 0 else .2)
                    shot('a4-keys-help-informational', 'RickyOptionList')
                    back()
                    shot('system-from-keys-top-key', 'Settings')
                back()
                shot('settings-from-' + name, 'Settings')
            back()
            shot('home-return-top-key', 'InxRecent')
            tap(.25 if orientation == 0 else .75, .76 if orientation == 0 else .53)
            shot('stats', 'RickyReadingStats')
            tap(.25, .87 if orientation == 0 else .91)
            shot('badges', 'RickyBadges')
            first = preview.get_frame()[3]
            for _ in range(18):
                preview.input({'type': 'key', 'key': 'DOWN', 'duration': 100})
                time.sleep(.15)
            shot('badges-last-page', 'RickyBadges')
            if preview.get_frame()[3] == first:
                raise RuntimeError('badge list did not page')
            back()
            shot('stats-from-badges', 'RickyReadingStats')
            tap(.8, .87 if orientation == 0 else .91)
            shot('month-report', 'RickyMonthReport')
            back()
            shot('stats-from-report', 'RickyReadingStats')
            back()
            shot('home-from-stats', 'InxRecent')
            for x, name, activity in [(.3, 'library', 'Library'), (.5, 'apps', 'AppsMenu'), (.7, 'storage', 'RickyStorage')]:
                tap(x, .94)
                shot(name, activity)
                back()
                shot('home-from-' + name + '-top-key', 'InxRecent')
            tap(.1, .94)
            shot('home-from-tabs', 'InxRecent')
            tap(.35, .48 if orientation == 0 else .55)
            shot('reader', 'EpubReader')
            preview.input({'type': 'touch', 'x1': .5, 'y1': .01, 'x2': .5, 'y2': .45, 'duration': 300})
            shot('reader-control-center', 'FrontlightPanel')
            back()
            shot('reader-from-control-center', 'EpubReader')
            tap(.5, .5)
            shot('reader-menu', 'EpubReaderMenu')
            back()
            shot('reader-from-menu-top-key', 'EpubReader')
            view_orientation = 0  # reader exit restores the product root orientation
            back()
            shot('home-from-reader-top-key', 'InxRecent')
            back()
            shot('home-standby-top-key', 'Standby')
            back()
            shot('home-awake-top-key', 'InxRecent')
            preview.input({'type': 'key', 'key': 'DOWN', 'duration': 120})
            shot('next-book-side-key', 'InxRecent')
        finally:
            preview.close()
            (out / (label + '-simulator.log')).write_bytes((state / 'simulator.log').read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/eego-preview'))
    out = parser.parse_args().out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    for orientation, label in [(0, 'portrait'), (1, 'landscape')]:
        run_case(out, orientation, label)
        for scenario in ['empty', 'long']:
            run_case(out, orientation, label + '-' + scenario, scenario)


if __name__ == '__main__':
    main()
