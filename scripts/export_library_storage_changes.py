#!/usr/bin/env python3
"""Export only the managed-library feature for isolated device-port integration.

No commits, pushes, source-pin upgrades, device access or other worktree writes.
The output is a generated patch, not a second maintained copy of the feature.
"""
import argparse
import difflib
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PATHS = (
    'lib/LibraryIndex/LibraryBuilder.h', 'lib/LibraryIndex/LibraryBuilder.cpp',
    'lib/LibraryIndex/LibrarySources.h', 'lib/LibraryIndex/LibrarySources.cpp',
    'lib/LibraryIndex/LibraryCategories.h', 'lib/LibraryIndex/LibraryCategories.cpp',
    'src/activities/library/LibraryCategoriesActivity.h', 'src/activities/library/LibraryCategoriesActivity.cpp',
    'src/activities/library/LibrarySourcesActivity.h', 'src/activities/library/LibrarySourcesActivity.cpp',
    'src/activities/library/LibraryListActivity.h', 'src/activities/library/LibraryListActivity.cpp',
    'src/activities/home/FileBrowserActivity.h', 'src/activities/home/FileBrowserActivity.cpp',
    'src/activities/home/RickyStorageActivity.cpp', 'src/activities/home/RickyStorageActivity.h', 'src/util/BootPreparation.cpp',
    'lib/I18n/translations/english.yaml', 'lib/I18n/translations/chinese.yaml',
)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--base', default='6ec61ccd',
                    help='pre-feature shared baseline (default: 6ec61ccd); never moving HEAD')
args = parser.parse_args()
base = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', '--verify', args.base + '^{commit}'], text=True).strip()
patch = []
for name in PATHS:
    before = subprocess.run(['git', '-C', str(ROOT), 'show', f'{base}:{name}'], capture_output=True)
    old = before.stdout.decode().splitlines(True) if before.returncode == 0 else []
    new = (ROOT / name).read_text().splitlines(True)
    patch.extend(difflib.unified_diff(old, new, fromfile=f'a/{name}' if old else '/dev/null', tofile=f'b/{name}'))
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(''.join(patch))
print(f'Exported {len(PATHS)} feature inputs since {base[:8]} to {args.output}')
