"""Keep large-library fixtures real/readable and outside user/device storage."""
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('library_stress', ROOT / 'tools/rickyos-browser/seed_library_stress.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


class LibraryStressFixtureTest(unittest.TestCase):
    def test_balanced_supported_formats_and_unique_names(self):
        generated = [fixtures.generated_book(i) for i in range(1, 7)]
        self.assertEqual([Path(name).suffix for name, _ in generated], ['.epub', '.txt', '.md'] * 2)
        self.assertEqual(len({name for name, _ in generated}), 6)
        self.assertTrue(all(len(data) > 1000 for _, data in generated))

    def test_epub_is_real_archive_with_spine_toc_and_readable_chapters(self):
        _, data = fixtures.generated_book(1)
        with zipfile.ZipFile(io.BytesIO(data)) as book:
            self.assertEqual(book.namelist()[0], 'mimetype')
            self.assertEqual(book.getinfo('mimetype').compress_type, zipfile.ZIP_STORED)
            self.assertEqual(book.read('mimetype'), b'application/epub+zip')
            self.assertIsNone(book.testzip())
            opf = ET.fromstring(book.read('OEBPS/content.opf'))
            ns = {'opf': 'http://www.idpf.org/2007/opf'}
            manifest = {item.attrib['id']: item.attrib['href'] for item in opf.findall('opf:manifest/opf:item', ns)}
            spine = opf.findall('opf:spine/opf:itemref', ns)
            self.assertEqual(len(spine), 4)
            for item in spine:
                chapter = ET.fromstring(book.read('OEBPS/' + manifest[item.attrib['idref']]))
                self.assertIn('压力测试', ''.join(chapter.itertext()))
            ET.fromstring(book.read('OEBPS/toc.ncx'))

    def test_image_epub_contains_valid_png_referenced_by_package(self):
        with zipfile.ZipFile(io.BytesIO(fixtures.epub(100, '含图测试'))) as book:
            self.assertTrue(book.read('OEBPS/cover.png').startswith(b'\x89PNG\r\n\x1a\n'))
            self.assertIn(b'cover.png', book.read('OEBPS/chapter1.xhtml'))
            self.assertIn(b'id="cover"', book.read('OEBPS/content.opf'))
            ET.fromstring(book.read('OEBPS/content.opf'))

    def test_long_utf8_names_and_real_markdown_table(self):
        title, data = fixtures.generated_book(17)
        self.assertIn('修订版', title)
        self.assertLess(len(title.encode()), 255)
        title, data = fixtures.generated_book(3)
        self.assertTrue(title.endswith('.md'))
        self.assertIn('| --- | --- |', data.decode())

    def test_refuses_arbitrary_directory_before_writing_any_books(self):
        with tempfile.TemporaryDirectory(prefix='ricky-stress-guard-') as temporary:
            with patch('sys.argv', ['seed', '--state-dir', temporary]), patch('sys.stderr', io.StringIO()):
                with self.assertRaises(SystemExit):
                    fixtures.main()
            self.assertEqual(list(Path(temporary).iterdir()), [])


if __name__ == '__main__':
    unittest.main()
