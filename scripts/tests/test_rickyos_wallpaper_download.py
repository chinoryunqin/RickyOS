"""RickyOS default standby pictures: catalog routes, BMP format and the on-device downloader."""
from pathlib import Path
import importlib.util
import io
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/build_rickyos_wallpapers.py'
ACTIVITY = ROOT / 'src/activities/apps/standby/RickyWallpaperDownloadActivity.cpp'


def body(source, signature):
    start = source.index(signature)
    depth = 0
    for index in range(source.index('{', start), len(source)):
        depth += {'{': 1, '}': -1}.get(source[index], 0)
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(signature)


def load_script():
    spec = importlib.util.spec_from_file_location('wallpapers', SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


try:
    from PIL import Image  # noqa: F401
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


class RickyWallpaperDownloadTest(unittest.TestCase):
    def test_catalog_routes_match_the_font_catalog_order(self):
        header = (ROOT / 'src/network/RickyWallpaperCatalog.h').read_text()
        repo = 'chinoryunqin/RickyOS-wallpapers'
        self.assertIn(f'"https://github.com/{repo}/releases/latest/download/wallpapers.json"', header)
        self.assertIn(f'"https://cdn.jsdelivr.net/gh/{repo}@latest/mirror.json"', header)
        self.assertIn(f'"https://fastly.jsdelivr.net/gh/{repo}@latest/mirror.json"', header)
        self.assertIn('china[MANIFEST_COUNT] = {MIRROR_MANIFEST, FASTLY_MANIFEST, GITHUB_MANIFEST}', header)
        self.assertIn('FOLDER[] = "/images/待机图片"', header)
        script = SCRIPT.read_text()
        self.assertIn(f'RELEASE_REPO = "{repo}"', script)
        self.assertIn('("mirror.json", jsdelivr, [fastly, github])', script)

    @unittest.skipUnless(HAVE_PIL, 'needs Pillow')
    def test_bmp_is_a_native_16_gray_4_bit_picture(self):
        from PIL import Image
        module = load_script()
        source = Image.linear_gradient('L').resize((1024, 1536))
        data = module.bmp4(module.quantize(module.fit_to_screen(source)))
        picture = Image.open(io.BytesIO(data))
        self.assertEqual(picture.size, (684, 1216))
        self.assertEqual(data[28], 4)  # biBitCount
        grays = {value for value in picture.convert('L').getdata()}
        self.assertTrue(grays <= {level * 17 for level in range(16)})
        self.assertGreater(len(grays), 12)

    def test_downloader_verifies_retries_and_never_replaces_a_chosen_picture(self):
        source = ACTIVITY.read_text()
        item = body(source, 'bool RickyWallpaperDownloadActivity::downloadItem(')
        self.assertIn('attempt < kFileAttempts && result == HttpDownloader::HTTP_ERROR', item)
        self.assertIn('size != item.size || crc != item.crc32', item)
        self.assertLess(item.index('Storage.remove(dest.c_str())'), item.index('Storage.rename(part.c_str(), dest.c_str())'))
        install = body(source, 'void RickyWallpaperDownloadActivity::installFirstIfUnset()')
        self.assertIn('Storage.exists(kStandbyPicture)) return;', install)
        self.assertIn('constexpr char kStandbyPicture[] = "/sleep.bmp";', source)
        self.assertIn('silentRestart();', body(source, 'void RickyWallpaperDownloadActivity::onExit()'))
        # Manifest names become card file names: no path separators.
        self.assertIn('strpbrk(name, "/\\\\:*?\\"<>|") == nullptr', source)

    def test_entry_points_in_standby_and_settings(self):
        standby = (ROOT / 'src/activities/apps/standby/StandbyActivity.cpp').read_text()
        self.assertIn('StandbyFace::PictureAction::Download', standby)
        self.assertIn('startActivityForResultWith<RickyWallpaperDownloadActivity>', standby)
        settings = (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text()
        self.assertIn('SettingAction::RickyWallpaperDownload', settings)
        self.assertIn('startActivityForResultWith<RickyWallpaperDownloadActivity>(resultHandler)', settings)
        for language in ('chinese', 'english'):
            strings = (ROOT / f'lib/I18n/translations/{language}.yaml').read_text()
            for key in ('DOWNLOAD', 'LOADING', 'PROGRESS', 'DONE', 'FAILED'):
                self.assertRegex(strings, rf'(?m)^STR_RICKY_WALLPAPER_{key}: "')


if __name__ == '__main__':
    unittest.main()
