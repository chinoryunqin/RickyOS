#!/usr/bin/env python3
"""Exercise a populated native preview through its real local input/frame API.

Start on Library / All categories / All books, page one. This is desktop-only;
latencies include injected gesture duration and are NOT physical device timings.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8790)
    parser.add_argument('--turns', type=int, default=80)
    parser.add_argument('--state-dir', type=Path, default=ROOT / '.cache/library-storage-preview')
    args = parser.parse_args()
    state = args.state_dir.resolve()
    if not state.is_relative_to(ROOT / '.cache') or not 1 <= args.turns <= 200 or not 1024 <= args.port <= 65524:
        parser.error('use an owned preview cache, local port, and 1..200 turns')
    base = f'http://127.0.0.1:{args.port}'
    html = urllib.request.urlopen(base, timeout=5).read().decode()
    token = re.search(r'<meta name="ricky-token" content="([^"]+)"', html).group(1)
    index = state / 'sd/.crosspoint/library.idx'
    index_before = hashlib.sha256(index.read_bytes()).hexdigest()
    log = state / 'simulator.log'
    log_start = log.stat().st_size
    output = state / 'stress-probes'
    output.mkdir(exist_ok=True)
    results = []
    activity_pattern = re.compile(rb'(?:Entering|Resuming) activity: ([\w-]+)')
    activity_matches = activity_pattern.findall(log.read_bytes())
    known_activity = activity_matches[-1].decode() if activity_matches else ''
    activity_cursor = log.stat().st_size

    def frame():
        with urllib.request.urlopen(base + '/api/frame', timeout=5) as response:
            return response.headers.get('ETag'), response.read()

    def shelf():
        with log.open('rb') as source:
            source.seek(max(0, log.stat().st_size - 32768))
            matches = re.findall(rb'body=.*?start=(\d+) count=(\d+) visible=(\d+)', source.read())
        return tuple(map(int, matches[-1])) if matches else None

    def action(label, data, expected_start=None, expected_activity=None):
        nonlocal known_activity, activity_cursor
        before, _ = frame()
        start = time.monotonic()
        request = urllib.request.Request(base + '/api/input', json.dumps(data).encode(),
            headers={'Content-Type': 'application/json', 'X-Ricky-Token': token})
        urllib.request.urlopen(request, timeout=5).read()
        time.sleep(data['duration'] / 1000 + .03)
        last, changed, first = before, None, None
        while time.monotonic() - start < 8:
            current, image = frame()
            now = time.monotonic()
            if current != last:
                last, changed = current, now
                if first is None:
                    first = now
            if current != before and changed is not None and now - changed >= .1:
                break
            time.sleep(.035)
        else:
            raise RuntimeError(f'{label}: no stable changed frame within 8 s')
        status = json.loads(urllib.request.urlopen(base + '/api/status', timeout=5).read())
        # The browser status inspects only its last 32 KB of logs, so its
        # activity field can legitimately be empty after many shelf repaints.
        # Track the actual native activity transitions incrementally instead.
        with log.open('rb') as source:
            source.seek(activity_cursor)
            activity_updates = activity_pattern.findall(source.read())
            activity_cursor = source.tell()
        if activity_updates:
            known_activity = activity_updates[-1].decode()
        if not status.get('alive') or (expected_activity and known_activity != expected_activity):
            raise RuntimeError(f'{label}: unexpected native activity {status}')
        position = shelf()
        if expected_start is not None and (not position or position[0] != expected_start):
            raise RuntimeError(f'{label}: unexpected shelf window {position}; expected {expected_start}')
        result = {'action': label, 'first_changed_ms': round((first - start) * 1000, 1),
                  'stable_ms': round((time.monotonic() - start) * 1000, 1),
                  'activity': known_activity, 'shelf': position}
        results.append(result)
        return image

    initial = shelf()
    if not initial or initial[0] != 0 or initial[1] < args.turns * 6 + 6:
        raise RuntimeError(f'Need page one and enough imported books; found {initial}')
    swipe = dict(type='touch', x1=.5, x2=.5, y1=.74, y2=.32, duration=140)
    for turn in range(1, args.turns + 1):
        image = action(f'next-{turn}', swipe, turn * 6, 'Library')
        if turn % 20 == 0:
            print(f'Forward {turn} pages, shelf start={turn * 6}', flush=True)
    (output / 'deep-page.png').write_bytes(image)
    swipe.update(y1=.32, y2=.74)
    for turn in range(args.turns - 1, -1, -1):
        image = action(f'previous-{turn}', swipe, turn * 6, 'Library')
    (output / 'returned-page-one.png').write_bytes(image)
    def tap(label, x, y, activity):
        return action(label, dict(type='touch', x1=x, x2=x, y1=y, y2=y, duration=100), expected_activity=activity)
    (output / 'reading-filter.png').write_bytes(tap('reading-filter', .32, .169, 'Library'))
    (output / 'unread-filter.png').write_bytes(tap('unread-filter', .5, .169, 'Library'))
    tap('all-filter', .14, .169, 'Library')
    (output / 'storage-entry.png').write_bytes(tap('storage-entry', .7, .95, 'RickyStorage'))
    (output / 'library-reentry.png').write_bytes(tap('library-reentry', .3, .95, 'Library'))
    if hashlib.sha256(index.read_bytes()).hexdigest() != index_before:
        raise RuntimeError('Ordinary navigation unexpectedly changed the library index')
    with log.open('rb') as source:
        source.seek(log_start)
        tail = source.read().decode(errors='replace')
    if re.search(r'\[LIBIDX\].*(?:built:|phase walk)', tail) or '[LIBSRC] import complete' in tail:
        raise RuntimeError('Ordinary navigation unexpectedly rebuilt the catalog')
    timings = [item['first_changed_ms'] for item in results]
    report = {'scope': 'desktop simulator only; includes gesture duration, not hardware latency',
              'books': initial[1], 'page_turns': args.turns * 2, 'actions': results,
              'first_changed_ms_median': round(statistics.median(timings), 1),
              'first_changed_ms_max': max(timings), 'index_unchanged': True, 'implicit_rebuilds': 0}
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'actions'}, indent=2), flush=True)


if __name__ == '__main__':
    main()
