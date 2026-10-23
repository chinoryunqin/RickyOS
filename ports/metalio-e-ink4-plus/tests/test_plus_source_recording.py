"""Source records must use and verify the separate pinned SDK baseline."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

PORT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('plus_sync', PORT / 'scripts/sync_plus_design.py')
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)


class RecordingTests(unittest.TestCase):
    def record(self, baseline, expected_success):
        with tempfile.TemporaryDirectory(prefix='plus-record-test-') as temporary:
            root = Path(temporary)
            upstream = root / 'upstream'
            name = 'freeink-sdk/libs/example.cpp'
            before, after = b'old SDK line\n', b'new SDK line\n'
            target = upstream / name
            target.parent.mkdir(parents=True)
            target.write_bytes(after)
            outputs = {key: root / (key.lower() + '.json') for key in ('MANIFEST', 'CHANGES', 'PATCH')}
            for file in outputs.values():
                file.write_text('existing record\n')
            manifest = {'commit': 'app-pin', 'sdk_commit': 'sdk-pin',
                        'files': {name: sync.sha(before)}}
            changes = {'changes': []}
            with patch.multiple(sync, UPSTREAM=upstream, **outputs), patch.object(sync, 'show', return_value=baseline) as show:
                if expected_success:
                    sync.finish(root / 'app', 'app-pin', manifest, changes,
                                manifest['files'], root / 'sdk')
                    show.assert_called_once_with(root / 'sdk', 'sdk-pin', 'libs/example.cpp')
                    record = json.loads(outputs['CHANGES'].read_text())['changes'][0]
                    self.assertEqual(record['baseline_sha256'], sync.sha(before))
                    self.assertEqual(record['ported_sha256'], sync.sha(after))
                    diff = outputs['PATCH'].read_text()
                    self.assertIn('-old SDK line\n+new SDK line\n', diff)
                    self.assertNotIn('/dev/null', diff)
                else:
                    with self.assertRaisesRegex(SystemExit, 'Missing/mismatched pinned source'):
                        sync.finish(root / 'app', 'app-pin', manifest, changes,
                                    manifest['files'], root / 'sdk')
                    for file in outputs.values():
                        self.assertEqual(file.read_text(), 'existing record\n')

    def test_explicit_sdk_baseline(self):
        self.record(b'old SDK line\n', True)

    def test_missing_baseline_cannot_overwrite_records(self):
        self.record(None, False)

    def test_mismatched_baseline_cannot_overwrite_records(self):
        self.record(b'wrong SDK checkout\n', False)


if __name__ == '__main__':
    unittest.main()
