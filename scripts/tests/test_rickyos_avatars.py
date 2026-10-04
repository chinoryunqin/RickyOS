"""RickyOS default avatars: line-art SVG sources, generated resources and storage."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_rickyos_navigation_icons import NODE, sharp_available

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/components/icons/rickyAvatars.h'
SCRIPT = ROOT / 'scripts/build_rickyos_avatars.cjs'
SOURCES = ROOT / 'src/images/sources/rickyos/avatars'
# Settings store "@avatar:<index>": this order is persistent and must never change.
ORDER = ['girl', 'boy', 'woman', 'man', 'mid_woman', 'mid_man', 'old_woman', 'old_man']


class RickyAvatarTest(unittest.TestCase):
    def test_eight_portraits_in_persistent_order(self):
        text = HEADER.read_text()
        self.assertIn('inline constexpr const uint8_t* ALL[] = {' + ', '.join(ORDER) + '};', text)
        self.assertIn('inline constexpr int COUNT = 8;', text)
        self.assertIn('inline constexpr int SIZE = 160;', text)
        for name in ORDER:
            self.assertTrue((SOURCES / f'{name}.svg').exists(), name)
            body = re.search(rf'uint8_t {name}\[\] = \{{([^}}]+)\}}', text).group(1)
            data = bytes(int(value, 16) for value in re.findall(r'0x([0-9A-F]{2})', body))
            self.assertEqual(len(data), 160 * 20, name)
            ink = sum(bin(byte ^ 0xFF).count('1') for byte in data)
            self.assertTrue(2000 < ink < 16000, name)  # line art, not a blank or a filled disc

    @unittest.skipUnless(sharp_available(), 'regeneration requires developer Node.js + sharp')
    def test_regeneration_matches_checked_in_header_and_sources(self):
        sources = {path.name: path.read_bytes() for path in SOURCES.glob('*.svg')}
        with tempfile.TemporaryDirectory(prefix='ricky-avatars-') as directory:
            output = Path(directory) / 'avatars.h'
            subprocess.run([NODE, str(SCRIPT), '--out', str(output)], check=True, capture_output=True)
            self.assertEqual(output.read_bytes(), HEADER.read_bytes())
        self.assertEqual({path.name: path.read_bytes() for path in SOURCES.glob('*.svg')}, sources)

    def test_avatar_setting_and_drawing(self):
        profile = (ROOT / 'src/components/RickyProfile.cpp').read_text()
        self.assertIn('constexpr char PRESET_PREFIX[] = "@avatar:";', profile)
        self.assertIn('return index < RickyAvatars::COUNT ? index : -1;', profile)
        # Area-sampled like the brand badge, never enlarged past the source.
        self.assertIn('const int drawn = std::min(size, SOURCE);', profile)
        self.assertIn('covered * 16 >= 7 * (sy1 - sy0) * (sx1 - sx0)', profile)
        self.assertIn('return setAvatar(target);', profile)  # imports share the rollback path
        picker = (ROOT / 'src/activities/settings/RickyAvatarPickerActivity.cpp').read_text()
        self.assertIn('snprintf(value, sizeof(value), "@avatar:%d", index - 1)', picker)
        self.assertIn('FileBrowserActivity::Mode::PickAvatar', picker)
        self.assertIn('int listCount() const override { return 1 + RickyAvatars::COUNT + 1; }',
                      (ROOT / 'src/activities/settings/RickyAvatarPickerActivity.h').read_text())


if __name__ == '__main__':
    unittest.main()
