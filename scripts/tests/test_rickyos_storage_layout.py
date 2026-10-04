"""RickyOS fixed folders and the one-time move of legacy books into /books."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def body(source, signature):
    start = source.index(signature)
    depth = 0
    for index in range(source.index('{', start), len(source)):
        depth += {'{': 1, '}': -1}.get(source[index], 0)
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(signature)


class RickyStorageLayoutTest(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / 'src/util/RickyStorageLayout.cpp').read_text()
        self.header = (ROOT / 'src/util/RickyStorageLayout.h').read_text()

    def test_fixed_folders(self):
        folders = dict(re.findall(r'(BOOKS|FONTS|IMAGES|DOWNLOADS) = "([^"]+)"', self.header))
        self.assertEqual(folders, {'BOOKS': '/books', 'FONTS': '/fonts', 'IMAGES': '/images', 'DOWNLOADS': '/downloads'})
        # /fonts is where the SD font registry looks for visible families.
        self.assertIn('FONTS_DIR_VISIBLE = "/fonts"', (ROOT / 'lib/EpdFont/SdCardFontRegistry.h').read_text())

    def test_move_runs_once_never_overwrites_and_carries_reading_data(self):
        move = body(self.source, 'int migrateLegacyBooks()')
        self.assertIn('if (Storage.exists(MIGRATION_MARKER)) return 0;', move)
        self.assertLess(move.index('Storage.exists(to.c_str())'), move.index('Storage.rename(from.c_str(), to.c_str())'))
        self.assertIn('relocateBookArtifacts(from, to)', move)
        self.assertIn('relocateBookReferences(from, to)', move)
        self.assertIn('if (isEmptyFolder(folder)) Storage.rmdir(folder);', move)
        self.assertIn('library::markLibraryIndexDirty()', move)
        self.assertNotIn('Storage.remove', self.source)
        self.assertIn('{"/book", "/Pushed Books"}', self.source)
        self.assertNotIn('WeRead', re.search(r'LEGACY_BOOK_FOLDERS\[\] = \{[^}]*\}', self.source).group(0))

    def test_boot_moves_after_recents_and_statistics_load(self):
        main = (ROOT / 'src/main.cpp').read_text()
        self.assertLess(main.index('RECENT_BOOKS.loadFromFile()'), main.index('RickyStorageLayout::migrateLegacyBooks()'))
        self.assertLess(main.index('READING_STATS.loadFromFile()'), main.index('RickyStorageLayout::migrateLegacyBooks()'))


if __name__ == '__main__':
    unittest.main()
