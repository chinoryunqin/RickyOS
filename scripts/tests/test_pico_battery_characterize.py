from pathlib import Path
import csv
import importlib.util
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'pico_battery_characterize.py'
SPEC = importlib.util.spec_from_file_location('battery_characterize', SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BatteryEvidenceTest(unittest.TestCase):
    def test_parser_preserves_zero_unknown_and_full_state(self):
        line = '[1234] [INF] [BATREF] mv=4000 pmu=0 curve=900 state=3 ext=1/1 shown=0 valid=1 source=4'
        row = MODULE.parse_sample(line)
        self.assertEqual(row['pmu_permille'], 0)
        self.assertEqual(row['shown_percent'], 0)
        self.assertEqual(row['charge_state'], 3)
        row = MODULE.parse_sample(line.replace('pmu=0 ', 'pmu=65535 ').replace('shown=0 ', 'shown=101 '))
        self.assertEqual(row['pmu_permille'], '')
        self.assertEqual(row['shown_percent'], '')
        self.assertIsNone(MODULE.parse_sample(line.replace('mv=4000', 'mv=4500')))
        self.assertIsNone(MODULE.parse_sample(line.replace('ext=1/1', 'ext=2/1')))
        self.assertIsNone(MODULE.parse_sample('[1234] [INF] [BAT] secret=not-battery-evidence'))

    def test_analyzer_cannot_claim_accuracy_from_two_voltage_estimates(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'boot.log'
            out = Path(directory) / 'samples.csv'
            log.write_text('[1] [INF] [BATREF] mv=3800 pmu=620 curve=620 state=1 ext=1/0 shown=62 valid=1 source=4\n'
                           '[2] [INF] [BATREF] mv=0 pmu=65535 curve=65535 state=0 ext=0/0 shown=62 valid=0 source=0\n')
            result = MODULE.analyze(log, out)
            self.assertEqual(result['observations'], 2)
            self.assertEqual(result['valid_observations'], 1)
            self.assertEqual(result['max_pmu_vs_curve_difference_pp'], 0)
            self.assertIs(result['accuracy_validated'], False)
            with out.open() as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(rows[1]['pmu_permille'], '')
            with self.assertRaises(FileExistsError):
                MODULE.analyze(log, out)


if __name__ == '__main__':
    unittest.main()
