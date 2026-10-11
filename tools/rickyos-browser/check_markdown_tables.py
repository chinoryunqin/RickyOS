#!/usr/bin/env python3
"""Private native table QA with an input document and an optional real MD3 baseline.

Only uses an owned temporary SD; no device, network upload, or publication.
"""
import argparse
import bisect
import json
import re
from pathlib import Path
import shutil
import struct
import tempfile
import time
import xml.etree.ElementTree as ET

from server import Preview, seed_demo


def read_toc(cache):
    data = (cache / 'book.bin').read_bytes()
    version, lut, spine_count, count = struct.unpack_from('<BIHH', data)
    assert version == 11 and spine_count == 1
    entries = []
    for index in range(count):
        position = struct.unpack_from('<I', data, lut + 4 * (spine_count + index))[0]
        strings = []
        for _ in range(3):
            size = struct.unpack_from('<I', data, position)[0]
            position += 4
            strings.append(data[position:position + size].decode())
            position += size
        level, spine = struct.unpack_from('<Bh', data, position)
        assert spine == 0
        entries.append((*strings, level))
    return entries


def visible_for_source(cache, source):
    data = (cache / 'txt-map.bin').read_bytes()
    records = [struct.unpack_from('<IIBBH', data, at) for at in range(16, len(data), 12)]
    record = records[bisect.bisect_right([r[0] for r in records], source) - 1]
    return record[1] + ((source - record[0]) // record[2] if record[3] else 0)


def run(state, output, sample, baseline):
    seed_demo(state / 'sd')
    sd = state / 'sd'
    shutil.copy2(sample, sd / 'books/表格验收.md')
    (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [
        {'path': '/books/表格验收.md', 'title': '表格验收', 'author': '', 'coverBmpPath': ''}]}))
    preview = Preview(state, 18786)
    current = preview.binary

    def log():
        return (state / 'simulator.log').read_text(errors='replace')

    def wait(predicate, label):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            assert preview.status()['alive'], log()[-4000:]
            if predicate():
                return
            time.sleep(0.1)
        raise AssertionError(label + '\n' + log()[-4000:])

    def key(name):
        preview.input({'type': 'key', 'key': name, 'duration': 150})

    def open_book():
        wait(lambda: preview.status()['input_ready'] and preview.get_frame(), 'boot')
        time.sleep(0.5)
        for _ in range(2):
            if 'Entering activity: EpubReader' in log():
                break
            key('ENTER')
            time.sleep(0.5)
        wait(lambda: 'Progress saved:' in log(), 'open readable MD')
        markers = list((sd / '.crosspoint').glob('epub_*/txt-cache-version'))
        assert len(markers) == 1
        return markers[0].parent

    def page():
        # The end-of-book panel belongs to EpubReader itself: it does not emit
        # an activity transition or a new page progress record (reader.cpp:1564).
        # Wait for a stable frame before pressing so idle AA is not mistaken
        # for the end panel, then distinguish page saves from that frame change.
        time.sleep(0.5)
        count = log().count('Progress saved:')
        before = preview.get_frame()[0]
        key('RIGHT')
        wait(lambda: log().count('Progress saved:') > count or
             preview.get_frame()[0] != before, 'page forward')
        time.sleep(0.3)
        return log().count('Progress saved:') > count

    try:
        if baseline:
            preview.stop()
            preview.binary = baseline.resolve()
            preview.start()
            cache = open_book()
            baseline_version = (cache / 'txt-cache-version').read_text()
            assert baseline_version in ('3', '4')
            page()
            old = (cache / 'progress.bin').read_bytes()
            assert len(old) == 10 and struct.unpack_from('<H', old, 2)[0] > 0
            mapping = (cache / 'txt-map.bin').read_bytes()
            records = [struct.unpack_from('<IIBBH', mapping, at) for at in range(16, len(mapping), 12)]
            visible = struct.unpack_from('<I', old, 6)[0]
            record = records[bisect.bisect_right([r[1] for r in records], visible) - 1]
            source = record[0] + ((visible - record[1]) * record[2] if record[3] else 0)
            preview.stop()
            (output / f'md{baseline_version}-baseline.log').write_text(log())
            preview.binary = current
            preview.start()
            cache = open_book()
            wait(lambda: not (cache / 'md-resume-v1.bin').exists(), 'checkpoint committed')
            assert (cache / 'txt-cache-version').read_text() == '5'
            progress = (cache / 'progress.bin').read_bytes()
            assert struct.unpack_from('<H', progress, 2)[0] > 0, 'upgrade reset to first page'
            (output / f'md{baseline_version}-upgrade.png').write_bytes(preview.get_frame()[0])
            (output / f'md{baseline_version}-upgrade.log').write_text(log())
            print(f'PASS: actual MD{baseline_version} baseline upgrade (old source={source})', flush=True)
            preview.stop()
            # Begin the visual walkthrough at the real first page; fixture-only
            # progress reset, never done to a user card or hardware file.
            (cache / 'progress.bin').write_bytes(struct.pack('<HHHI', 0, 0, 1, 1))
            preview.start()

        cache = open_book()
        html = (cache / 'html/0.html').read_text()
        body = ET.fromstring(html)
        tables = body.findall('.//table')
        assert tables and all(table.find('tr/th') is not None for table in tables)
        headings = [node for node in body.iter() if re.fullmatch('h[1-6]', node.tag)]
        toc = read_toc(cache)
        assert len(toc) == len(headings) + 1, (len(toc), len(headings))
        for entry, heading in zip(toc[1:], headings):
            title, href, anchor, level = entry
            assert title == ''.join(heading.itertext()) and level == int(heading.tag[1]), entry
            assert href == 'content.html' and body.find(f'.//div[@id="{anchor}"]') is not None
        (output / 'toc.json').write_text(json.dumps(toc, ensure_ascii=False, indent=2))
        print(f'PASS: {len(headings)} heading labels, levels and source anchors', flush=True)

        # Use the actual default list menu and its physical key routing.
        # Account for a preface/BOM before the first heading, just as the
        # production currentTocIndex source-map lookup does.
        target = min(10, len(toc) - 1)
        assert target > 1, 'sample needs multiple headings for navigation QA'
        visible = struct.unpack_from('<I', (cache / 'progress.bin').read_bytes(), 6)[0]
        data = (cache / 'txt-map.bin').read_bytes()
        records = [struct.unpack_from('<IIBBH', data, at) for at in range(16, len(data), 12)]
        position = bisect.bisect_right([r[1] for r in records], visible) - 1
        selected = 0  # Initial page offset 0 precedes the source map's first visible offset 1.
        if position >= 0:
            record = records[position]
            source_position = record[0] + ((visible - record[1]) * record[2] if record[3] else 0)
            selected = bisect.bisect_right([int(e[2].split('-')[1]) for e in toc[1:]], source_position)
        key('ENTER')
        wait(lambda: 'Entering activity: EpubReaderMenu' in log(), 'reader menu')
        key('ENTER')
        wait(lambda: 'Entering activity: EpubReaderChapterSelection' in log(), 'contents list')
        time.sleep(0.4)
        (output / 'toc-list.png').write_bytes(preview.get_frame()[0])
        for _ in range(abs(target - selected)):
            key('DOWN' if target > selected else 'UP')
            time.sleep(0.25)
        time.sleep(0.3)
        (output / 'toc-selected.png').write_bytes(preview.get_frame()[0])
        print(f'TOC navigation: initial={selected}, desired={target}, visible={visible}', flush=True)
        before = log().count('Progress saved:')
        key('ENTER')
        wait(lambda: log().count('Progress saved:') > before, 'heading jump saved')
        time.sleep(0.5)  # ignore transitional progress while a partial section extends
        assert 'Failed to resolve TXT chapter' not in log()
        (output / 'toc-jump.png').write_bytes(preview.get_frame()[0])
        key('BACK')
        wait(lambda: 'Exiting activity: EpubReader' in log(), 'persist partial section after jump')
        time.sleep(0.2)
        target_visible = visible_for_source(cache, int(toc[target][2].split('-')[1]))
        progress = (cache / 'progress.bin').read_bytes()
        actual_page = struct.unpack_from('<H', progress, 2)[0]
        saved_visible = struct.unpack_from('<I', progress, 6)[0]
        section = next((cache / 'sections').glob('*.bin')).read_bytes()
        # Section's documented native POD header: 24 bytes of render spec,
        # uint16 page count, followed by five uint32 LUT offsets (46 total).
        page_count = struct.unpack_from('<H', section, 24)[0]
        lut = struct.unpack_from('<I', section, 42)[0]
        starts = struct.unpack_from(f'<{page_count}I', section, lut)
        expected_page = bisect.bisect_right(starts, target_visible) - 1
        assert actual_page == expected_page and saved_visible == starts[actual_page], (
            target, target_visible, actual_page, expected_page, saved_visible)
        print(f'PASS: selected TOC row {target}, resolved to containing page {actual_page}', flush=True)
        preview.stop()
        (output / 'toc-jump.log').write_text(log())
        # Reset only this owned QA fixture; never touch a user SD/progress.
        (cache / 'progress.bin').write_bytes(struct.pack('<HHHI', 0, 0, 1, 1))
        preview.start()
        cache = open_book()
        (output / 'converted.html').write_text(html)
        print(f'PASS: parsed {len(tables)} tables; starting physical-resolution walkthrough', flush=True)
        for index in range(40):
            (output / f'page-{index + 1:02}.png').write_bytes(preview.get_frame()[0])
            if not page():
                (output / 'end-of-book.png').write_bytes(preview.get_frame()[0])
                break
        else:
            raise AssertionError('did not reach end within 40 pages')
        assert 'Failed to load EPUB' not in log()
        assert 'Index build failed' not in log()
        print(f'PASS: reached document end after {index + 1} pages', flush=True)
        key('BACK')
        preview.stop()
        (output / 'table-walkthrough.log').write_text(log())
        settings_path = sd / '.crosspoint/settings.json'
        settings = json.loads(settings_path.read_text())
        # This product's built-in high-DPI font ID is fixed (Settings.cpp:954);
        # changing fontSize without an SD family cannot prove font reflow.
        # Change a real pagination key instead, then require a cache rebuild.
        settings['lineSpacing'] = 2 if settings.get('lineSpacing', 1) != 2 else 0
        settings_path.write_text(json.dumps(settings))
        preview.start()
        cache = open_book()
        assert (cache / 'txt-cache-version').read_text() == '5'
        assert 'Cache not found, building...' in log(), 'layout change reused stale pagination'
        (output / 'layout-reflow.png').write_bytes(preview.get_frame()[0])
        print('PASS: reopen and changed-line-spacing reflow', flush=True)
    finally:
        preview.close()
        (output / 'table-last.log').write_text(log())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sample', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ricky-md-table-qa-') as directory:
        run(Path(directory), args.output, args.sample, args.baseline)


if __name__ == '__main__':
    main()
