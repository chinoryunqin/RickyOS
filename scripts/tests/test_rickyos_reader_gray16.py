"""RickyOS anti-aliased pages: one 16-level render, text-turn commits, native JPEGs."""
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
READER = ROOT / 'src/activities/reader/EpubReaderActivity.cpp'


def load_patch():
    spec = importlib.util.spec_from_file_location('patch_rickyos_epdiy', ROOT / 'scripts/patch_rickyos_epdiy.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pristine(path):
    """The SDK file as pinned, before the build-time patch."""
    rel = Path(path).relative_to(ROOT / 'freeink-sdk')
    return subprocess.run(['git', '-C', str(ROOT / 'freeink-sdk'), 'show', f'HEAD:{rel}'],
                          check=True, capture_output=True, text=True).stdout


class RickyReaderGray16Test(unittest.TestCase):
    def test_rickyos_takes_the_16_level_path_for_every_face_bold_and_jpeg_pages(self):
        source = READER.read_text()
        start = source.index('#else\n  // RickyOS: every text face')
        rule = source[start:source.index('#endif', start)]
        self.assertIn('page->imagesAreAllJpeg()', rule)
        self.assertNotIn('fakeBold', rule)
        self.assertNotIn('readerFaceIsFourBit', rule)
        # The B/W pre-render is skipped and the commit picks text turn or the GC16 cleanup.
        self.assertIn('if (use16LevelText) {\n    // The 16-level pass below clears the frame', source)
        self.assertIn('display.setNextGray16Profile(refresh == HalDisplay::FAST_REFRESH ? 1 : 2);', source)
        # Cached 16-level pages are copied, not re-rendered.
        self.assertIn('memcpy(renderer.grayscale16Frame(), pageCache16_[pageCacheLiveSlot_].get()', source)
        self.assertIn('renderer.beginGrayscale16Offscreen(pageCache16_[slot].get())', source)

    def test_epdiy_patch_applies_once_and_refuses_changed_sources(self):
        patch = load_patch()
        cpp = ROOT / 'freeink-sdk/libs/display/EpdiyLcd/src/EpdiyLcd.cpp'
        header = ROOT / 'freeink-sdk/libs/display/EpdiyLcd/include/EpdiyLcd.h'
        for path, old, new in ((cpp, patch.OLD_COMMIT, patch.NEW_COMMIT), (header, patch.OLD_DECL, patch.NEW_DECL)):
            text = pristine(path)
            patched = patch.patch_text(text, old, new)
            self.assertIn(patch.MARK, patched)
            self.assertEqual(patch.patch_text(patched, old, new), patched)  # re-runnable
            with self.assertRaises(RuntimeError):
                patch.patch_text(text.replace(old, old.replace('bwProxy', 'proxy')), old, new)
        # Reader turns keep the rails up; long-lived frames still power down.
        self.assertIn('const bool turnOff = g_gray16Profile == 0 || g_gray16Profile == 3;', patch.NEW_COMMIT)

    def test_jpeg_detection_for_native_images(self):
        header = (ROOT / 'lib/Epub/Epub/blocks/ImageBlock.h').read_text()
        start = header.index('  static bool isJpegPath(')
        body = header[start:header.index('\n  }\n', start) + 4]
        program = '#include <cassert>\n#include <string>\nstruct ImageBlock {\n' + body + '};\nint main(){\n' + \
            ' assert(ImageBlock::isJpegPath("a/b.jpg") && ImageBlock::isJpegPath("x.JPEG"));\n' + \
            ' assert(!ImageBlock::isJpegPath("x.png") && !ImageBlock::isJpegPath("noext"));\n}\n'
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'check.cpp'
            src.write_text(program)
            exe = Path(tmp) / 'check'
            subprocess.run(['c++', '-std=c++20', '-Wall', '-Werror', str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_glyph_native_branch_is_rickyos_only(self):
        source = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        start = source.index('#ifdef RICKYOS_PRODUCT\n    else if ((is2Bit || is4Bit) && renderer.isGrayscale16Active()')
        branch = source[start:source.index('#endif', start)]
        self.assertIn('dilate2BitCoverage', branch)
        self.assertIn('draw4BitGlyphPixel(renderer, screenX, screenY, static_cast<uint8_t>(coverage * 5))', branch)


if __name__ == '__main__':
    unittest.main()
