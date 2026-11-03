#!/usr/bin/env python3
"""Restore this device branch's tested overlay onto its pinned FreeInk SDK."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=root / 'freeink-sdk')
    parser.add_argument('--check', action='store_true', help='Verify without changing SDK files')
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    bundle = root / 'patches/metalio-eink4'
    manifest = json.loads((bundle / 'freeink-sdk.json').read_text())
    patch = bundle / 'freeink-sdk.patch'
    if hashlib.sha256(patch.read_bytes()).hexdigest() != manifest['patch_sha256']:
        raise SystemExit('SDK patch differs from its recorded digest')
    if not (sdk / '.git').exists():
        raise SystemExit('SDK is not initialized; run: git submodule update --init freeink-sdk')
    head = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    if head != manifest['base_commit']:
        raise SystemExit('SDK HEAD differs from the pinned base; preserve local work and use the branch SDK revision')

    def mismatches():
        result = []
        for name, expected in manifest['files'].items():
            path = sdk / name
            if expected is None:
                valid = not path.exists()
            else:
                valid = path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == expected
            if not valid:
                result.append(name)
        return result

    if not mismatches():
        print(f'SDK overlay verified: {len(manifest["files"])} paths match the tested snapshot')
        return
    if args.check:
        raise SystemExit('SDK overlay is missing or changed; run this script without --check on a clean pinned SDK')
    command = ['git', '-C', str(sdk), 'apply', '--whitespace=nowarn']
    # A complete preflight protects local changes; never reset or force an overlay.
    subprocess.run([*command, '--check', str(patch)], check=True)
    subprocess.run([*command, str(patch)], check=True)
    failures = mismatches()
    if failures:
        raise SystemExit('Applied SDK differs from the snapshot: ' + ', '.join(failures))
    print(f'SDK overlay restored and verified: {len(manifest["files"])} paths')


if __name__ == '__main__':
    main()
