#!/usr/bin/env python3
"""Fail closed on an unreviewed Plus IDF, before compiling and before packaging."""
import argparse
import json
from pathlib import Path
import subprocess

EXPECTED = '9a97f6c54ec638111ce55cd36581b3c192f15207'


def verify(idf, project=None):
    idf = Path(idf).resolve(strict=True)
    revision = subprocess.check_output(['git', '-C', str(idf), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != EXPECTED:
        raise ValueError(f'Wrong ESP-IDF {revision}; Plus requires {EXPECTED}')
    dirty = subprocess.check_output(['git', '-C', str(idf), 'status', '--porcelain',
                                     '--untracked-files=normal'], text=True).strip()
    if dirty:
        raise ValueError('Plus ESP-IDF checkout is dirty; use the reviewed clean commit')
    if project is not None:
        description = json.loads(Path(project).read_text())
        if description.get('git_revision') not in (EXPECTED, EXPECTED[:8]):
            raise ValueError('Build description does not identify the reviewed ESP-IDF')
        if Path(description.get('idf_path', '')).resolve() != idf:
            raise ValueError('Build used a different IDF path')
        if description.get('target') != 'esp32s31':
            raise ValueError('Build target is not esp32s31')
    return idf


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf', required=True, type=Path)
    parser.add_argument('--project', type=Path)
    args = parser.parse_args()
    try:
        verify(args.idf, args.project)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')
    print(f'Clean Plus ESP-IDF {EXPECTED} verified')
