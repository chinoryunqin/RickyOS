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

    @unittest.skipUnless(HAVE_PIL, 'needs Pillow')
    def test_slogans_sit_under_the_doodle_and_clear_of_the_time_corner(self):
        from PIL import Image, ImageDraw, ImageOps
        module = load_script()
        font = ROOT / '.cache/rickyos-font-sources/smiley/SmileySans-Oblique.otf'
        if not font.exists():
            self.skipTest('Smiley Sans source not downloaded')
        page = Image.new('L', (module.WIDTH, module.HEIGHT), 255)
        ImageDraw.Draw(page).ellipse((240, 260, 440, 520), outline=0, width=6)  # the "doodle"
        result = module.add_slogan(page, ('先看一页', '再看亿页'), font)
        text = ImageOps.invert(result.crop((0, 540, module.WIDTH, module.HEIGHT))).point(lambda v: 255 if v > 60 else 0)
        box = text.getbbox()
        self.assertIsNotNone(box)
        self.assertLessEqual(540 + box[3], round(module.HEIGHT * module.SLOGAN_BOTTOM))
        for entry in module.WALLPAPERS:
            if len(entry) == 4:
                self.assertTrue(all(len(line) <= 6 for line in entry[3]), entry)
        # Light pictures only: the dark night and lamp scenes are gone.
        stems = [entry[0] for entry in module.WALLPAPERS]
        self.assertNotIn('04-moon', stems)
        self.assertNotIn('06-lamp', stems)

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

    def test_newest_manifest_wins_over_stale_cdn_caches(self):
        source = ACTIVITY.read_text()
        fetch = body(source, 'bool RickyWallpaperDownloadActivity::fetchManifest()')
        # Every route is read; none short-circuits after the first success.
        self.assertIn('for (int attempt = 0; attempt < RickyWallpaperCatalog::MANIFEST_COUNT; ++attempt)', fetch)
        self.assertNotIn('result != HttpDownloader::OK; ++attempt', fetch)
        self.assertIn('RickyWallpaperCatalog::releaseOf(base)', fetch)
        self.assertIn('release <= bestRelease) continue;', fetch)
        header = (ROOT / 'src/network/RickyWallpaperCatalog.h').read_text()
        self.assertIn('static_assert(releaseOf("https://cdn.jsdelivr.net/gh/x/y@v1.1.0/wallpapers/") == 0x010100);', header)

    @unittest.skipUnless(HAVE_PIL, 'needs Pillow')
    def test_published_pictures_are_small_4_bit_pngs(self):
        from PIL import Image
        module = load_script()
        self.assertTrue(all(entry[2].endswith('.png') for entry in module.WALLPAPERS))
        page = Image.new('L', (module.WIDTH, module.HEIGHT), 255)
        data = module.png4(module.quantize(page))
        self.assertEqual(data[24], 4)   # IHDR bit depth
        self.assertEqual(data[25], 3)   # palette
        self.assertLess(len(data), 20_000)

    def test_png_pictures_install_as_8_bit_gray_and_cdn_hosts_go_first(self):
        source = ACTIVITY.read_text()
        install = body(source, 'void RickyWallpaperDownloadActivity::installFirstIfUnset()')
        self.assertIn('PngToBmpConverter::pngFileToGray8BmpFile(first.c_str(), kStandbyPicture, true)', install)
        self.assertIn('SETTINGS.sleepScreen = CrossPointSettings::CUSTOM;', install)
        fetch = body(source, 'bool RickyWallpaperDownloadActivity::fetchManifest()')
        self.assertIn('host.find("jsdelivr.net") != std::string::npos', fetch)
        self.assertIn('FsHelpers::hasPngExtension(std::string_view(file))', fetch)
        item = body(source, 'bool RickyWallpaperDownloadActivity::downloadItem(')
        self.assertIn('if (result == HttpDownloader::OK) preferredHost_ = host;', item)
        viewer = (ROOT / 'src/activities/util/ImageViewerActivity.cpp').read_text()
        preview = body(viewer, 'bool ImageViewerActivity::preparePreview()')
        # Converted aside into the preview cache, then renamed into place.
        self.assertIn('pngFileToGray8BmpFile(filePath.c_str(), outPath.c_str(), true)', preview)
        self.assertIn('Storage.rename(outPath.c_str(), previewPath.c_str())', preview)

    def test_entry_points_in_standby_and_settings(self):
        standby = (ROOT / 'src/activities/apps/standby/StandbyActivity.cpp').read_text()
        self.assertIn('StandbyFace::PictureAction::Download', standby)
        self.assertIn('startActivityForResultWith<RickyWallpaperDownloadActivity>', standby)
        page = (ROOT / 'src/activities/apps/standby/RickyStandbySettingsActivity.cpp').read_text()
        self.assertIn('startActivityForResultWith<RickyWallpaperDownloadActivity>', page)
        for language in ('chinese', 'english'):
            strings = (ROOT / f'lib/I18n/translations/{language}.yaml').read_text()
            for key in ('DOWNLOAD', 'LOADING', 'PROGRESS', 'DONE', 'FAILED'):
                self.assertRegex(strings, rf'(?m)^STR_RICKY_WALLPAPER_{key}: "')


if __name__ == '__main__':
    unittest.main()
