#!/usr/bin/env python3
"""Native Markdown open/page/reopen and synthetic MD2-cache upgrade regression.

Uses an owned temporary demo SD card only; no USB/device or publication access.
"""
import argparse
import html
import json
from pathlib import Path
import struct
import tempfile
import time

from server import Preview, seed_demo

SOURCE = ('# 阅读与生活\n\n中文 **粗体** 和 *斜体*，以及 `code()`。\n\n'
          '- 清晨，读一页\n- 夜晚，再读一页\n\n1. 打开一本书\n2. 留一点时间\n\n'
          '> 慢一点，读几页。\n\n---\n\n```\n  hello <world>\n  **保留代码符号**\n```\n\n'
          + ''.join(f'## 安静的第 {index} 页\n\n' + '给阅读留一点时间。一本书，一杯茶，一个安静的下午。\n\n' * 4
                    for index in range(1, 20)))


def old_cache_fixture(cache, text):
    """Construct the documented MD2 plain-text layout; not a hardware acceptance."""
    body = html.escape(text, quote=False).replace('\n', '<br />')
    (cache / 'html/0.html').write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<!-- MD_CACHE_VERSION: 2 -->\n'
        '<!DOCTYPE html>\n<html>\n<head><title>MD体验</title></head>\n<body>\n' + body + '\n</body>\n</html>\n')
    records = []
    source, visible, previous = 0, 1, None
    target = text.encode().index('## 安静的第 10 页'.encode())
    target_visible = None
    for char in text:
        width, step = len(char.encode()), int(char != '\n')
        if source == target:
            target_visible = visible
        if (width, step) != previous:
            records.append(struct.pack('<IIBBH', source, visible, width, step, 0))
            previous = (width, step)
        source += width
        visible += step
    records.append(struct.pack('<IIBBH', source, visible, 0, 0, 0))
    (cache / 'txt-map.bin').write_bytes(struct.pack('<IIIHBB', 0x4D545854, source, len(records), 12, 1, 1)
                                         + b''.join(records))
    (cache / 'txt-cache-version').write_text('2')
    (cache / 'progress.bin').write_bytes(struct.pack('<HHHI', 0, 3, 30, target_visible))
    return target


def run(state, output):
    seed_demo(state / 'sd')
    sd = state / 'sd'
    (sd / 'books/MD体验.md').write_text(SOURCE, encoding='utf-8')
    (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [
        {'path': '/books/MD体验.md', 'title': 'MD体验', 'author': '', 'coverBmpPath': ''}]}), encoding='utf-8')
    preview = Preview(state, 18776)

    def log():
        return (state / 'simulator.log').read_text(errors='replace')

    def wait(predicate, message):
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            assert preview.status()['alive'], log()[-4000:]
            if predicate():
                return
            time.sleep(0.1)
        raise AssertionError(message + '\n' + log()[-4000:])

    def key(name):
        preview.input({'type': 'key', 'key': name, 'duration': 150})

    def open_book():
        wait(lambda: preview.status()['input_ready'] and preview.get_frame(), 'boot')
        time.sleep(0.5)
        if 'Entering activity: EpubReader' not in log():
            key('ENTER')  # First Confirm can move focus from tabs to Home content.
            time.sleep(0.5)
            if 'Entering activity: EpubReader' not in log():
                key('ENTER')
        wait(lambda: 'Entering activity: EpubReader' in log(), 'open MD')
        wait(lambda: 'Progress saved:' in log(), 'first readable page')
        assert 'Failed to load EPUB' not in log(), log()[-4000:]

    try:
        open_book()
        caches = list((sd / '.crosspoint').glob('epub_*/txt-cache-version'))
        assert len(caches) == 1, caches
        cache = caches[0].parent
        assert caches[0].read_text() == '5'
        converted = (cache / 'html/0.html').read_text()
        for tag in ('<h1>', '<strong>', '<em>', '<ul>', '<ol>', '<blockquote>', '<code>'):
            assert tag in converted, tag
        time.sleep(0.5)
        (output / 'markdown-first-page.png').write_bytes(preview.get_frame()[0])
        before = log().count('Progress saved:')
        key('RIGHT')
        wait(lambda: log().count('Progress saved:') > before, 'page forward')
        (output / 'markdown-page-turn.png').write_bytes(preview.get_frame()[0])
        key('BACK')
        wait(lambda: 'Exiting activity: EpubReader' in log(), 'exit MD')
        (output / 'markdown-open-page.log').write_text(log())
        preview.stop()

        # Isolated synthetic upgrade: same book/source, prior 1.1.4 plaintext map
        # and a saved visible coordinate. Real physical upgrade is still required.
        source = old_cache_fixture(cache, SOURCE)
        preview.start()
        open_book()
        wait(lambda: not (cache / 'md-resume-v1.bin').exists(), 'new progress committed')
        progress = (cache / 'progress.bin').read_bytes()
        assert len(progress) == 10 and struct.unpack_from('<H', progress, 2)[0] > 0, 'Upgrade reset to page zero'
        assert (cache / 'txt-cache-version').read_text() == '5'
        assert '<strong>粗体</strong>' in (cache / 'html/0.html').read_text()
        time.sleep(0.5)
        (output / 'markdown-upgraded-page.png').write_bytes(preview.get_frame()[0])
        (output / 'markdown-upgrade.log').write_text(log())
        print(f'PASS: MD open, pagination and MD2 checkpoint upgrade (source={source})')
        preview.stop()
        # Real first-page saves can contain a zero visible offset, while maps
        # begin at 1. This is a known book start, not a corrupt coordinate.
        old_cache_fixture(cache, SOURCE)
        (cache / 'progress.bin').write_bytes(struct.pack('<HHHI', 0, 0, 30, 0))
        preview.start()
        open_book()
        assert (cache / 'txt-cache-version').read_text() == '5'
        progress = (cache / 'progress.bin').read_bytes()
        assert struct.unpack_from('<H', progress, 2)[0] == 0
        assert not (cache / 'md-resume-v1.bin').exists()
        print('PASS: first-page zero-offset upgrade opens without progress error')
    finally:
        preview.close()
        (output / 'markdown-last.log').write_text(log())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'MD体验.md').write_text(SOURCE, encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='ricky-md-qa-') as directory:
        run(Path(directory), args.output)


if __name__ == '__main__':
    main()
