"""RickyOS anti-aliased pages: one 16-level render, text-turn commits, native JPEGs."""
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
READER = ROOT / 'src/activities/reader/EpubReaderActivity.cpp'


def sdk(path):
    """The vendored SDK file (RickyOS changes included since 2026-10-19)."""
    return Path(path).read_text()


class RickyReaderGray16Test(unittest.TestCase):
    def test_rickyos_takes_the_16_level_path_for_every_face_bold_and_jpeg_pages(self):
        source = READER.read_text()
        start = source.index('  // RickyOS: every text face')
        rule = source[start:source.index('const bool needsAnyGrayscale', start)]
        self.assertIn('page->imagesAreAllJpeg()', rule)
        self.assertNotIn('fakeBold', rule)
        self.assertNotIn('readerFaceIsFourBit', rule)
        # The B/W pre-render is skipped and the commit picks text turn or the GC16 cleanup.
        self.assertIn('if (use16LevelText) {\n    // The 16-level pass below clears the frame', source)
        self.assertIn('display.setNextGray16Profile(refresh == HalDisplay::FAST_REFRESH ? 1 : 2);', source)
        # Cached 16-level pages are copied, not re-rendered.
        self.assertIn('memcpy(renderer.grayscale16Frame(), pageCache16_[pageCacheLiveSlot_].get()', source)
        self.assertIn('renderer.beginGrayscale16Offscreen(pageCache16_[slot].get())', source)

    def test_epdiy_carries_the_text_turn_commit_once(self):
        cpp = sdk(ROOT / 'freeink-sdk/libs/display/EpdiyLcd/src/EpdiyLcd.cpp')
        header = sdk(ROOT / 'freeink-sdk/libs/display/EpdiyLcd/include/EpdiyLcd.h')
        self.assertIn('RICKYOS_GRAY16_TEXT_TURN', cpp)
        for decl in ('void epdiyLcdSetGray16Profile(uint8_t profile);', 'void epdiyLcdRailsOffIfIdle(uint32_t idleMs);',
                     'bool epdiyLcdRestart();'):
            self.assertEqual(header.count(decl), 1)
        # Reader turns keep the rails up; long-lived frames still power down.
        self.assertIn('const bool turnOff = g_gray16Profile == 0 || g_gray16Profile == 3;', cpp)

    def test_rails_drop_after_frames_stop(self):
        cpp = sdk(ROOT / 'freeink-sdk/libs/display/EpdiyLcd/src/EpdiyLcd.cpp')
        self.assertIn('g_railsUp = true;', cpp)
        self.assertIn('void epdiyLcdRailsOffIfIdle(uint32_t idleMs) {', cpp)
        # Held-up rails let charge build in the film (old pages show through): the main
        # loop drops them once frames stop, never during a push.
        manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
        self.assertIn('RenderLock railsLock(RenderLock::Mode::Try);', manager)
        self.assertIn('if (railsLock.ownsLock()) display.railsOffIfIdle(3000);', manager)

    def test_line_queues_start_each_phase_empty_and_repair_rebuilds_the_scan_path(self):
        cpp = sdk(ROOT / 'freeink-sdk/libs/display/EpdiyLcd/src/EpdiyLcd.cpp')
        ctx = sdk(ROOT / 'freeink-sdk/libs/display/EpdiyLcd/src/epdiy/src/output_common/render_context.c')
        # Lines a late interrupt left in a queue shifted every later phase (a lighter copy of
        # the page beside itself until reboot): both queues are emptied before each phase,
        # before the feed threads start on it.
        prepare = ctx[ctx.index('void IRAM_ATTR prepare_context_for_next_frame(RenderContext_t* ctx) {'):]
        self.assertLess(prepare.index('lq_reset(lq);'), prepare.index('ctx->lines_prepared = 0;'))
        self.assertIn(' stale=%u/%u', cpp)
        self.assertIn('bool epdiyLcdRestart() {', cpp)
        repair = (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text()
        repair = repair[repair.index('case SettingAction::RickyScreenRepair: {'):]
        self.assertLess(repair.index('display.restartPanel();'), repair.index('renderer.clearScreen(0x00);'))

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
        start = source.index('else if ((is2Bit || is4Bit) && renderer.isGrayscale16Active()')
        branch = source[start:source.index('else if (is2Bit || is4Bit) {', start)]
        self.assertIn('dilate2BitCoverage', branch)
        self.assertIn('draw4BitGlyphPixel(renderer, screenX, screenY, static_cast<uint8_t>(coverage * 5))', branch)


if __name__ == '__main__':
    unittest.main()
