#!/usr/bin/env python3
"""Exercise the real native standby input paths on an isolated temporary SD card.

No USB access, firmware writes or publication. Build simulator_rickyos first.
"""
import argparse
import json
from pathlib import Path
import tempfile
import time

from server import Preview, seed_demo


def check(preview, enabled):
    log_path = preview.state_dir / 'simulator.log'

    def log():
        return log_path.read_text(errors='replace')

    def wait_for(predicate, description):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if not preview.status()['alive']:
                raise AssertionError('Native simulator exited: ' + log()[-4000:])
            if predicate():
                return
            time.sleep(0.05)
        raise AssertionError('Timed out: ' + description + '\n' + log()[-4000:])

    def key(name):
        preview.input({'type': 'key', 'key': name, 'duration': 150})

    def unchanged(before):
        time.sleep(1.2)
        for event in ('Entering activity: Standby', 'Exiting activity: Standby'):
            assert log().count(event) == before.count(event), event

    wait_for(lambda: preview.status()['input_ready'] and preview.get_frame() is not None,
             'boot and input bridge')
    wait_for(lambda: 'Entering activity: InxRecent' in log(), 'Home')
    settings = json.loads((preview.sd / '.crosspoint/settings.json').read_text())
    assert settings['standbyShortcutEnabled'] == int(enabled), 'Saved switch changed at boot'
    # Real main.cpp deliberately ignores power gestures for two seconds after boot.
    time.sleep(2.2)
    before = log()
    key('BACK')
    if enabled:
        wait_for(lambda: log().count('Entering activity: Standby') ==
                 before.count('Entering activity: Standby') + 1, 'middle-key entry')
        unchanged(log())  # Entry release must not also dismiss Standby.
        before = log()
        key('BACK')
        wait_for(lambda: log().count('Exiting activity: Standby') ==
                 before.count('Exiting activity: Standby') + 1, 'middle-key exit')
        unchanged(log())
    else:
        unchanged(before)  # Disabled on Home, not only inside Standby.

    before = log()
    key('POWER')
    wait_for(lambda: log().count('Entering activity: Standby') ==
             before.count('Entering activity: Standby') + 1, 'independent side-key entry')
    unchanged(log())
    if not enabled:
        for _ in range(3):
            before = log()
            key('BACK')
            unchanged(before)
    before = log()
    key('POWER')
    wait_for(lambda: log().count('Exiting activity: Standby') ==
             before.count('Exiting activity: Standby') + 1, 'independent side-key wake')
    unchanged(log())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Owned QA log directory')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for index, enabled in enumerate((False, True, False)):
        case = 'on' if enabled else 'off'
        with tempfile.TemporaryDirectory(prefix='ricky-middle-qa-') as directory:
            state = Path(directory)
            seed_demo(state / 'sd')
            settings_path = state / 'sd/.crosspoint/settings.json'
            settings = json.loads(settings_path.read_text())
            # 1.1.4 migrates legacy cards without this marker to the on default.
            # Test current persisted settings, not that unrelated migration.
            settings['rickyStandbyKeyOn'] = 1
            settings['standbyShortcutEnabled'] = int(enabled)
            settings_path.write_text(json.dumps(settings), encoding='utf-8')
            preview = Preview(state, 18775)
            try:
                check(preview, enabled)
                print('PASS: middle-key switch ' + case + '; side power remains independent')
            finally:
                preview.close()
                (args.output / (f'middle-key-{index}-{case}.log')).write_bytes(
                    (state / 'simulator.log').read_bytes())


if __name__ == '__main__':
    main()
