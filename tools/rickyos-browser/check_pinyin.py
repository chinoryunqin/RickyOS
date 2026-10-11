#!/usr/bin/env python3
"""Real simulator keyboard QA, isolated SD state; never connects to hardware."""
import json
from pathlib import Path
import tempfile
import time
from server import Preview, seed_demo


def main():
    out = Path('build/pinyin/screenshots')
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ricky-ime-qa-') as directory:
        state = Path(directory)
        seed_demo(state / 'sd')
        config = state / 'sd/.crosspoint/settings.json'
        settings = json.loads(config.read_text())
        settings.update(rickyNickname='', language='ZH_CN', clockAutoSync=0, rickyAutoOffIndex=0)
        config.write_text(json.dumps(settings, ensure_ascii=False))
        preview = Preview(state, 18830)
        try:
            def shot(label, activity):
                deadline = time.monotonic() + 12
                time.sleep(.4)
                while time.monotonic() < deadline:
                    status, frame = preview.status(), preview.get_frame()
                    if status['activity'] == activity and status['input_ready'] and frame:
                        (out / (label + '.png')).write_bytes(frame[0])
                        return
                    time.sleep(.1)
                raise AssertionError((label, status))

            def tap(x, y):
                preview.input({'type': 'touch', 'x1': x, 'y1': y, 'x2': x, 'y2': y, 'duration': 100})
                time.sleep(.4)

            shot('01-home', 'InxRecent')
            tap(.87, .10)
            shot('02-profile', 'RickyProfile')
            tap(.4, .405)
            shot('03-keyboard', 'KeyboardEntry')
            # Observed portrait key centers, not production hit testing. The
            # firmware itself registers/draws every key with the SDK geometry.
            letters = {
                'n': (.695, .858), 'i': (.742, .694), 'h': (.597, .777),
                'a': (.115, .777), 'o': (.836, .694),
                's': (.17, .777), 't': (.45, .694),
            }
            for letter in 'nihao':
                tap(*letters[letter])
            shot('04-nihao-candidates', 'KeyboardEntry')
            tap(.07, .531)
            shot('05-committed', 'KeyboardEntry')
            # Completing is a separate gesture, never an automatic side effect
            # of selecting the word.
            tap(.87, .943)
            shot('06-saved-profile', 'RickyProfile')
            saved = json.loads(config.read_text())
            assert saved['rickyNickname'] == '你好', saved.get('rickyNickname')
            # Reopen and cancel a pending syllable; committed nickname survives.
            tap(.4, .405)
            shot('07-reopened', 'KeyboardEntry')
            tap(*letters['n'])
            preview.input({'type': 'key', 'key': 'BACK', 'duration': 100})
            shot('08-back-clears-composition', 'KeyboardEntry')
            preview.input({'type': 'key', 'key': 'BACK', 'duration': 100})
            shot('09-back-cancels-editor', 'RickyProfile')
            assert json.loads(config.read_text())['rickyNickname'] == '你好'
            tap(.4, .405)
            shot('10-switch-mode-editor', 'KeyboardEntry')
            tap(.292, .943)
            shot('11-english-mode', 'KeyboardEntry')
            for letter in 'nihao':
                tap(*letters[letter])
            tap(.87, .943)
            shot('12-english-saved', 'RickyProfile')
            assert json.loads(config.read_text())['rickyNickname'] == '你好nihao'
            tap(.4, .405)
            shot('13-literal-editor', 'KeyboardEntry')
            for letter in 'nihao':
                tap(*letters[letter])
            tap(.925, .531)  # Explicit abc: commit Latin, not the first Han word.
            shot('14-literal-committed', 'KeyboardEntry')
            tap(.87, .943)
            shot('15-literal-saved', 'RickyProfile')
            assert json.loads(config.read_text())['rickyNickname'] == '你好nihaonihao'
            print('PASS: Han commit/save, Back ownership, English switch and explicit Latin commit;', out, flush=True)
        finally:
            preview.stop()
            preview.socket.close()
            preview.state_lock.close()


if __name__ == '__main__':
    main()
