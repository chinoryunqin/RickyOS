"""Product startup is read/write-free and does not depend on bundled EPUB data."""
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


class ProductUserGuideTest(unittest.TestCase):
    def check_source(self, source):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            binary = output / 'guide-test'
            deps = output / 'guide.d'
            obj = output / 'guide.o'
            subprocess.run([
                'clang++', '-std=c++20', '-DRICKYOS_PRODUCT=1', '-DNDEBUG',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I' + str(ROOT / 'test/user_guide/stubs'),
                '-I' + str(source.parent), str(source),
                str(ROOT / 'test/user_guide/ProductUserGuideTest.cpp'),
                '-o', str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
            subprocess.run([
                'clang++', '-std=c++20', '-DRICKYOS_PRODUCT=1',
                '-MMD', '-MF', str(deps), '-c', str(source), '-o', str(obj),
            ], check=True)
            dependencies = deps.read_text()
            self.assertNotIn('generated.h', dependencies)
            self.assertNotIn('HalStorage', dependencies)
            self.assertNotIn('RecentBooksStore', dependencies)

    def test_shared_product_startup(self):
        self.check_source(ROOT / 'src/util/UserGuide.cpp')

    def test_plus_product_startup(self):
        source = ROOT / 'ports/metalio-e-ink4-plus/firmware/rickyos-plus-1.2/upstream/src/util/UserGuide.cpp'
        if not source.exists():
            self.skipTest('Restore the pinned Plus snapshot first')
        self.check_source(source)

    def test_build_hook(self):
        run_script = runpy.run_path
        for defines, should_bundle in [
            ([], True), ([('RICKYOS_PRODUCT', 0)], True),
            ([('RICKYOS_PRODUCT', '1')], False),
            ([['RICKYOS_PRODUCT', 1]], False), (['RICKYOS_PRODUCT'], False),
        ]:
            with self.subTest(defines=defines):
                class Env(dict):
                    def ParseFlags(self, flags):
                        return {'CPPDEFINES': defines}

                    def subst(self, value):
                        return str(ROOT)

                with patch('runpy.run_path') as generator:
                    with patch('sys.path', list(__import__('sys').path)):
                        run_script(str(ROOT / 'scripts/build_userguide.py'),
                                   init_globals={'Import': lambda name: None, 'env': Env()})
                    self.assertEqual(generator.called, should_bundle)
                    self.assertEqual(generator.return_value.__getitem__.return_value.called, should_bundle)

    @unittest.skipUnless((ROOT / 'ports/metalio-e-ink4-plus').is_dir(), 'Plus port lives on its own branch')
    def test_plus_resource_hook_does_not_bundle(self):
        hook = (ROOT / 'ports/metalio-e-ink4-plus/scripts/prepare_rickyos_plus_resources.sh').read_text()
        self.assertNotIn('generate_userguide_epub.py', hook)
        self.assertNotIn('UserGuide.generated.h', hook)


if __name__ == '__main__':
    unittest.main()
