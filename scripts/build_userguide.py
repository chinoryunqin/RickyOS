"""PlatformIO hook shared by firmware and simulator; stdlib only."""
import runpy
import sys
from pathlib import Path

Import("env")

defines = env.ParseFlags(env.get("BUILD_FLAGS", [])).get("CPPDEFINES", [])
product = any(
    define == "RICKYOS_PRODUCT" or
    (isinstance(define, (tuple, list)) and define[0] == "RICKYOS_PRODUCT"
     and str(define[1]) != "0")
    for define in defines
)
if not product:
    root = Path(env.subst("$PROJECT_DIR"))
    sys.path.insert(0, str(root / 'scripts'))
    generator = runpy.run_path(str(root / 'scripts/generate_userguide_epub.py'))
    generator['build_bundled'](root / 'docs/user-guide', root / 'build/user-guide',
                               root / 'src/util/UserGuide.generated.h')
