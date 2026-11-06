#!/usr/bin/env python3
"""Check explicit Chinese settings translations and six-size built-in glyph coverage."""

import importlib.util
from pathlib import Path
import sys
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("gen_i18n", ROOT / "scripts/gen_i18n.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def outline_codepoints(header):
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", header.read_text()))
    count = struct.unpack_from(">H", data, 4)[0]
    cmap = next(struct.unpack_from(">I", data, at + 8)[0] for at in range(12, 12 + 16 * count, 16)
                if data[at:at + 4] == b"cmap")
    covered = set()
    for record in range(struct.unpack_from(">H", data, cmap + 2)[0]):
        table = cmap + struct.unpack_from(">I", data, cmap + 4 + 8 * record + 4)[0]
        format_id = struct.unpack_from(">H", data, table)[0]
        if format_id == 12:
            groups = struct.unpack_from(">I", data, table + 12)[0]
            for at in range(table + 16, table + 16 + 12 * groups, 12):
                first, last, glyph = struct.unpack_from(">III", data, at)
                covered.update(range(first + (glyph == 0), last + 1))
        elif format_id == 4:
            segments = struct.unpack_from(">H", data, table + 6)[0] // 2
            ends = table + 14
            starts = ends + 2 * segments + 2
            deltas = starts + 2 * segments
            ranges = deltas + 2 * segments
            for segment in range(segments):
                first = struct.unpack_from(">H", data, starts + 2 * segment)[0]
                last = struct.unpack_from(">H", data, ends + 2 * segment)[0]
                delta = struct.unpack_from(">h", data, deltas + 2 * segment)[0]
                offset = struct.unpack_from(">H", data, ranges + 2 * segment)[0]
                for codepoint in range(first, min(last, 0xFFFE) + 1):
                    glyph = codepoint if offset == 0 else struct.unpack_from(
                        ">H", data, ranges + 2 * segment + offset + 2 * (codepoint - first))[0]
                    if (offset == 0 or glyph != 0) and (glyph + delta) & 0xFFFF:
                        covered.add(codepoint)
    return covered


class SettingsTranslationsTest(unittest.TestCase):
    metalio_native = '--metalio-native' in sys.argv
    if metalio_native:
        sys.argv.remove('--metalio-native')

    def test_settings_and_glyph_coverage(self):
        english = generator.parse_yaml_file(str(ROOT / "lib/I18n/translations/english.yaml"))
        chinese = generator.parse_yaml_file(str(ROOT / "lib/I18n/translations/chinese.yaml"))
        sources = [ROOT / "src/SettingsList.h", ROOT / "src/HomeButtonSettings.h"]
        sources += list((ROOT / "src/activities/settings").glob("*.cpp"))
        sources += list((ROOT / "src/activities/settings").glob("*.h"))
        keys = set().union(*(set(re.findall(r"\bSTR_[A-Z0-9_]+\b", path.read_text())) for path in sources))
        self.assertFalse(keys - chinese.keys(), sorted(keys - chinese.keys()))
        for key in english:
            if key.startswith(("STR_THEME_", "STR_SPACING_")):
                self.assertEqual(chinese[key], english[key], key)
        self.assertEqual(chinese["STR_INX_TAB_POSITION"], "Inx 标签栏位置")

        cjk = lambda text: set(re.findall(r"[一-鿿]", text))
        required = cjk((ROOT / "lib/I18n/translations/chinese.yaml").read_text())
        if self.metalio_native:
            # Active interface roles use independently sized SC/TC outlines.
            for code, language in (("sc_regular", "chinese"), ("tc_regular", "chinese-traditional")):
                translated = generator.parse_yaml_file(str(ROOT / f"lib/I18n/translations/{language}.yaml"))
                expected = cjk("".join(translated.values()))
                coverage = outline_codepoints(ROOT / f"src/resources/SystemUiFont_{code}.h")
                missing = {ord(char) for char in expected} - coverage
                self.assertFalse(missing, (language, "".join(map(chr, sorted(missing)))))
            calendar = cjk((ROOT / "lib/EpdFont/scripts/cn_almanac_chars.txt").read_text())
            header = (ROOT / "lib/EpdFont/builtinFonts/metalio_body_10_regular.h").read_text()
            self.assert_cjk_coverage(header, calendar, "metalio calendar fallback")
            return

        required |= cjk((ROOT / "lib/EpdFont/scripts/cn_almanac_chars.txt").read_text())
        common = cjk((ROOT / "lib/EpdFont/scripts/chars_3500_common.txt").read_text()) | required
        for name, expected in (("cn_common_chars.txt", common), ("cn_i18n_chars.txt", required)):
            self.assertEqual(cjk((ROOT / "lib/EpdFont/scripts" / name).read_text()), expected, name)
        for size in (8, 10, 12, 14, 16, 18):
            name = "notosans_cjk_common_intervals.h" if size <= 12 else f"notosans_cjk_{size}.h"
            header = (ROOT / "lib/EpdFont/builtinFonts" / name).read_text()
            self.assert_cjk_coverage(header, common if size <= 12 else required, name)

    def assert_cjk_coverage(self, header, expected, name):
        intervals = re.search(r"Intervals\[\] = \{(.*?)\n\};", header, re.S).group(1)
        codepoints = set()
        for first, last in re.findall(r"\{\s*(0x[0-9A-F]+),\s*(0x[0-9A-F]+),", intervals):
            codepoints.update(range(int(first, 16), int(last, 16) + 1))
        missing = {ord(char) for char in expected} - codepoints
        self.assertFalse(missing, (name, "".join(map(chr, sorted(missing)))))


if __name__ == "__main__":
    unittest.main()
