"""RickyOS Standby: the user's picture in native 16-gray with an optional date/time corner."""
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


class RickyStandbyWallpaperTest(unittest.TestCase):
    def setUp(self):
        self.face = (ROOT / 'src/activities/apps/standby/WallpaperFace.cpp').read_text()
        self.activity = (ROOT / 'src/activities/apps/standby/StandbyActivity.cpp').read_text()

    def test_product_offers_only_the_wallpaper_face(self):
        table = self.activity.split('constexpr FaceEntry kFaces[] = {', 1)[1].split('\n};', 1)[0]
        product, stock = table.split('#else', 1)
        self.assertIn('makeUniqueNoThrow<WallpaperFace>()', product)
        self.assertNotIn('SloppyClockFace', product)
        self.assertNotIn('ChineseCalendarFace', product)
        self.assertIn('makeUniqueNoThrow<SloppyClockFace>()', stock)

    def test_picture_is_the_settings_wallpaper_in_native_16_gray(self):
        self.assertIn('constexpr char kPicture[] = "/sleep.bmp";', self.face)
        self.assertIn('constexpr const char* SLEEP_IMAGE_PATH = "/sleep.bmp";',
                      (ROOT / 'src/activities/util/ImageViewerActivity.cpp').read_text())
        native = body(self.face, 'bool WallpaperFace::renderNative(')
        order = [native.index(call) for call in ('beginGrayscale16()', 'drawBitmapGrayscale16(',
                                                 'drawCorner(renderer, viewport, true)', 'commitGrayscale16()')]
        self.assertEqual(order, sorted(order))
        self.assertIn('renderer.cancelGrayscale16();', native)

    def test_minute_updates_touch_only_the_corner(self):
        native = body(self.face, 'bool WallpaperFace::renderNative(')
        partial = native.split('if (pictureShown_ && !fullRedraw_) {', 1)[1].split('} else {', 1)[0]
        self.assertIn('drawCorner(renderer, viewport, false)', partial)
        self.assertIn('renderer.displayBuffer(HalDisplay::FAST_REFRESH)', partial)
        self.assertNotIn('Grayscale16', partial)
        tick = body(self.face, 'StandbyFace::TickResult WallpaperFace::tick()')
        self.assertIn('if (dayChanged || local.tm_min == 0) fullRedraw_ = true;', tick)
        corner = body(self.face, 'void WallpaperFace::drawCorner(')
        # Tabular digits size every minute, so a partial update covers the last one.
        self.assertIn('getTextWidth(kTimeFont, "00:00", EpdFontFamily::BOLD)', corner)
        self.assertIn('renderer.copyBwToGrayscale16(x, y, width, height, kCornerRadius)', corner)

    def test_activity_is_full_screen_and_syncs_only_for_a_clock(self):
        enter = body(self.activity, 'void StandbyActivity::onEnter()')
        self.assertIn('mode_ = DisplayMode::Immersive;', enter.split('#else', 1)[0])
        self.assertIn('currentFace_->wantsClock()', enter)
        self.assertIn('if (currentFace_->needsPicture()) return false;', body(self.activity, 'bool StandbyActivity::tryLightSleep('))
        render = body(self.activity, 'void StandbyActivity::render(')
        self.assertLess(render.index('renderNative(renderer, viewport)'), render.index('renderer.clearScreen();'))
        picker = body(self.activity, 'void StandbyActivity::openPicturePicker()')
        self.assertIn('FileBrowserActivity::Mode::PickWallpaper', picker)
        self.assertIn('startActivityForResultWith<ImageViewerActivity>(reload, entry->path, true)', picker)

    def test_overlay_setting_is_persisted_and_translated(self):
        header = (ROOT / 'src/CrossPointSettings.h').read_text()
        self.assertIn('uint8_t standbyOverlay = STANDBY_OVERLAY_TIME;', header)
        settings = (ROOT / 'src/SettingsList.h').read_text()
        entry = settings[settings.index('&CrossPointSettings::standbyOverlay'):][:400]
        self.assertIn('"standbyOverlay"', entry)
        self.assertEqual(len(re.findall(r'STR_RICKY_STANDBY_INFO_\w+', entry)), 3)
        for language in ('chinese', 'english'):
            strings = (ROOT / f'lib/I18n/translations/{language}.yaml').read_text()
            for key in ('STR_RICKY_STANDBY_INFO', 'STR_RICKY_STANDBY_INFO_NONE', 'STR_RICKY_STANDBY_INFO_DATE',
                        'STR_RICKY_STANDBY_INFO_TIME', 'STR_RICKY_STANDBY_EMPTY'):
                self.assertRegex(strings, rf'(?m)^{key}: "')
        self.assertIn('setting.valuePtr == &CrossPointSettings::standbyOverlay',
                      (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
