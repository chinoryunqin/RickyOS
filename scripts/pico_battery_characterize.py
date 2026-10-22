#!/usr/bin/env python3
"""Read-only Pico battery evidence capture; never equates estimates with true SOC.

Capture: python scripts/pico_battery_characterize.py capture --port /dev/cu.usbmodem101 --out samples.csv
Existing logs: python scripts/pico_battery_characterize.py analyze boot.log --out samples.csv
The output is local test evidence, not a calibration table ready for production.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time

FIELDS = ('observed_utc', 'device_ms', 'millivolts', 'pmu_permille', 'curve_permille',
          'charge_state', 'external_known', 'external', 'shown_percent', 'valid', 'source')


def parse_sample(line, observed_utc=''):
    match = re.search(r'\[(\d+)\].*?\[BATREF\]\s+(.*)', line)
    if not match:
        return None
    fields = dict(re.findall(r'(\w+)=(-?\d+(?:/-?\d+)?)', match[2]))
    try:
        mv, pmu, curve, charge, shown, valid, source = (
            int(fields[key]) for key in ('mv', 'pmu', 'curve', 'state', 'shown', 'valid', 'source'))
        ext_known, external = map(int, fields['ext'].split('/'))
    except (KeyError, ValueError):
        return None
    if valid not in (0, 1) or ext_known not in (0, 1) or external not in (0, 1):
        return None
    if not 0 <= source <= 255 or not 0 <= charge <= 255 or not (0 <= shown <= 100 or shown == 101):
        return None
    if pmu != 65535 and not 0 <= pmu <= 1000:
        return None
    if curve != 65535 and not 0 <= curve <= 1000:
        return None
    if valid and not 2700 <= mv <= 4400:
        return None
    return dict(zip(FIELDS, (observed_utc, int(match[1]), mv,
                             '' if pmu == 65535 else pmu, '' if curve == 65535 else curve,
                             charge, ext_known, external if ext_known else '',
                             '' if shown == 101 else shown, valid, source)))


def summarize(samples):
    valid = [row for row in samples if row['valid']]
    discrepancies = [abs(row['pmu_permille']-row['curve_permille']) / 10
                     for row in valid if row['pmu_permille'] != '' and row['curve_permille'] != '']
    return {
        'observations': len(samples), 'valid_observations': len(valid),
        'charge_states': sorted({row['charge_state'] for row in valid}),
        'max_pmu_vs_curve_difference_pp': max(discrepancies) if discrepancies else None,
        'accuracy_validated': False,
        'limitation': 'PMU and curve share voltage input; neither is an independent remaining-capacity reference. '
                      'Repeated records may contain the same cached PMU sample.',
    }


def analyze(input_path, output_path):
    samples = []
    # Exclusive creation: never overwrite an earlier discharge run.
    with output_path.open('x', newline='') as output:
        writer = csv.DictWriter(output, FIELDS)
        writer.writeheader()
        with input_path.open(errors='replace') as log:
            for line in log:
                sample = parse_sample(line)
                if sample is not None:
                    samples.append(sample)
                    writer.writerow(sample)
    return summarize(samples)


def capture(port, output_path, seconds):
    import serial

    class NoResetSerial(serial.Serial):
        # A normal serial open may reset an ESP via DTR/RTS. Do not change them.
        def _update_dtr_state(self):
            pass

        def _update_rts_state(self):
            pass

    samples = []
    with output_path.open('x', newline='') as output, NoResetSerial(port, 115200, timeout=0.2) as device:
        writer = csv.DictWriter(output, FIELDS)
        writer.writeheader()
        deadline = time.monotonic() + seconds
        pending = b''
        while time.monotonic() < deadline:
            pending += device.read(512)
            while b'\n' in pending:
                raw, pending = pending.split(b'\n', 1)
                sample = parse_sample(raw.decode(errors='replace'), datetime.now(timezone.utc).isoformat())
                if sample is not None:
                    samples.append(sample)
                    writer.writerow(sample)
                    output.flush()
            if len(pending) > 8192:
                pending = b''  # Bound malformed serial traffic; never collect unrelated logs.
    return summarize(samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest='command', required=True)
    offline = subcommands.add_parser('analyze')
    offline.add_argument('log', type=Path)
    offline.add_argument('--out', required=True, type=Path)
    live = subcommands.add_parser('capture')
    live.add_argument('--port', required=True)
    live.add_argument('--out', required=True, type=Path)
    live.add_argument('--seconds', type=int, default=300)
    args = parser.parse_args()
    if args.command == 'capture':
        if not 1 <= args.seconds <= 86400:
            parser.error('--seconds must be between 1 and 86400')
        result = capture(args.port, args.out, args.seconds)
    else:
        result = analyze(args.log, args.out)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
