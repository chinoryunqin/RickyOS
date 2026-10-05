"""RickyOS reading statistics: one page, the logo's dog reacting to today's reading."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PAGE = ROOT / 'src/activities/apps/reading-stats/RickyReadingStatsActivity.cpp'


def body(source, signature):
    start = source.index(signature)
    depth = 0
    for index in range(source.index('{', start), len(source)):
        depth += {'{': 1, '}': -1}.get(source[index], 0)
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(signature)


class RickyReadingStatsTest(unittest.TestCase):
    def test_apps_entry_opens_the_single_page(self):
        menu = (ROOT / 'src/activities/apps/AppsMenuActivity.cpp').read_text()
        product = menu.split('#ifdef RICKYOS_PRODUCT\n    {AppId::ReadingStats', 1)[1].split('#else', 1)[0]
        self.assertIn('&ActivityManager::goToRickyReadingStats', product)
        header = (ROOT / 'src/activities/apps/reading-stats/RickyReadingStatsActivity.h').read_text()
        self.assertIn('int listCount() const override { return 0; }', header)  # nothing to drill into

    def test_mascot_follows_today_against_the_goal(self):
        page = PAGE.read_text()
        build = body(page, 'void RickyReadingStatsActivity::buildScreen(')
        self.assertIn('todayMs < kMinuteMs ? ricky_mascot_sleeping : (reached ? ricky_mascot_happy : ricky_mascot_reading)',
                      re.sub(r'\s+', ' ', build))
        icons = (ROOT / 'src/components/icons/rickyMascotIcons.h').read_text()
        for name in ('sleeping', 'reading', 'happy'):
            data = re.search(rf'ricky_mascot_{name}\[\] = \{{([^}}]+)\}}', icons).group(1)
            self.assertEqual(len(re.findall(r'0x[0-9A-F]{2}', data)), 19 * 150)

    def test_week_starts_monday_and_facts_use_real_data(self):
        page = PAGE.read_text()
        self.assertIn('return static_cast<int>((dayOrdinal + 3) % 7);', page)  # 1970-01-01 was a Thursday
        fact = body(page, 'std::string dailyFact(')
        for source in ('getSessionLog()', 'getReadingDays()', 'getBooks()', 'getBooksFinishedCount()'):
            self.assertIn(source, fact)
        self.assertIn('return facts[today % facts.size()];', fact)

    def test_strings_exist_and_streak_names_the_goal(self):
        keys = set(re.findall(r'STR_RICKY_STATS_\w+', PAGE.read_text()))
        self.assertGreater(len(keys), 15)
        for language in ('chinese', 'english'):
            strings = (ROOT / f'lib/I18n/translations/{language}.yaml').read_text()
            for key in keys:
                self.assertRegex(strings, rf'(?m)^{key}: "', (language, key))
        self.assertIn('STR_RICKY_STATS_STREAK: "连续达标', (ROOT / 'lib/I18n/translations/chinese.yaml').read_text())


if __name__ == '__main__':
    unittest.main()
