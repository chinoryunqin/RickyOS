#!/usr/bin/env python3
"""Capture a device's serial log and summarize its PERF lines.

Perf builds (env rickyos_readpico_perf / rickyos_eego_a4_perf, or Plus with
RICKYOS_PERF=1) print one line per measured event:

    [12345] [INF] [PERF] <event> [<label>] key=123 key=45 ...

Numbers are milliseconds unless the key says otherwise (kb, kbps, b, n). The label
(a bare word right after the event, e.g. the activity name) splits an event into rows.

  capture: python3 tools/rickyos-perf/perf.py capture --port /dev/cu.usbmodem101 --seconds 300 --out pico.log
  summary: python3 tools/rickyos-perf/perf.py summary pico.log [a4.log plus.log] [--csv out.csv]
"""
import argparse
import csv
import re
import statistics
import sys
import time
from collections import defaultdict
from pathlib import Path

LINE = re.compile(r'\[(\d+)\] \[INF\] \[PERF\] (\S+)(.*)')
PAIR = re.compile(r'(\w+)=(-?\d+)\b')


def capture(port_name, seconds, out_path):
    import serial  # pyserial, from the PlatformIO env

    end = time.time() + seconds
    port = None
    lines = 0
    with open(out_path, 'wb') as out:
        while time.time() < end:
            if port is None:
                try:
                    port = serial.Serial(port_name, 115200, timeout=0.5)
                except Exception:
                    time.sleep(0.3)
                    continue
            try:
                data = port.read(4096)
            except Exception:  # the board resets or the cable drops: reopen
                port = None
                time.sleep(0.3)
                continue
            if data:
                out.write(data)
                out.flush()
                for raw in data.split(b'\n'):
                    if b'[PERF]' in raw:
                        lines += 1
                        print(raw.decode('utf-8', 'replace').strip(), flush=True)
    print(f'{lines} PERF lines -> {out_path}', file=sys.stderr)


def parse(path):
    rows = defaultdict(lambda: defaultdict(list))
    for text in Path(path).read_text('utf-8', 'replace').splitlines():
        match = LINE.search(text)
        if not match:
            continue
        event, rest = match.group(2), match.group(3)
        words = rest.split()
        label = words[0] if words and '=' not in words[0] else ''
        for key, value in PAIR.findall(rest):
            rows[(event, label)][key].append(int(value))
    return rows


def pct(values, p):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(p / 100 * (len(ordered) - 1))))]


def summary(paths, csv_path):
    table = []
    for path in paths:
        device = Path(path).stem
        for (event, label), keys in sorted(parse(path).items()):
            for key, values in keys.items():
                table.append([device, event, label, key, len(values), int(statistics.median(values)),
                              pct(values, 90), max(values)])
    head = ['device', 'event', 'label', 'key', 'n', 'median', 'p90', 'max']
    widths = [max(len(str(row[i])) for row in table + [head]) for i in range(len(head))]
    for row in [head] + table:
        print('  '.join(str(cell).ljust(width) for cell, width in zip(row, widths)))
    if csv_path:
        with open(csv_path, 'w', newline='') as out:
            csv.writer(out).writerows([head] + table)


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='cmd', required=True)
    cap = sub.add_parser('capture')
    cap.add_argument('--port', required=True)
    cap.add_argument('--seconds', type=float, default=300)
    cap.add_argument('--out', required=True)
    summ = sub.add_parser('summary')
    summ.add_argument('logs', nargs='+')
    summ.add_argument('--csv')
    args = parser.parse_args()
    if args.cmd == 'capture':
        capture(args.port, args.seconds, args.out)
    else:
        summary(args.logs, args.csv)


if __name__ == '__main__':
    main()
