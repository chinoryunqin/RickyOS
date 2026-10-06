"""RickyOS Standby clock digits: generated header matches its generator and font."""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class RickyClockDigitsTest(unittest.TestCase):
    def test_header_matches_generator(self):
        spec = importlib.util.spec_from_file_location('gen', ROOT / 'scripts/gen_ricky_clock_digits.py')
        gen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gen)
        header = (ROOT / 'src/components/fonts/rickyClockDigits.h').read_text()
        cap = int(header.split('kCapHeight = ')[1].split(';')[0])
        cell, glyphs = gen.render(ROOT / 'scripts/fonts/InterDisplay-Light.ttf', cap)
        self.assertEqual(header, gen.header(cap, cell, glyphs))
        self.assertEqual(set(glyphs), set('0123456789:'))
        # The font ships with its licence (SIL OFL 1.1).
        self.assertIn('SIL Open Font License', (ROOT / 'scripts/fonts/Inter-LICENSE.txt').read_text())

    def test_standby_picture_clock_is_drawn_into_the_gray_frame(self):
        face = (ROOT / 'src/activities/apps/standby/WallpaperFace.cpp').read_text()
        start = face.index('  if (intoGray) {')
        block = face[start:face.index('    return;\n  }', start)]
        self.assertIn('RickyClockDigits::fadeToPaper', block)
        self.assertIn('RickyClockDigits::draw', block)
        self.assertIn('copyBwToGrayscale16', block)


if __name__ == '__main__':
    unittest.main()
