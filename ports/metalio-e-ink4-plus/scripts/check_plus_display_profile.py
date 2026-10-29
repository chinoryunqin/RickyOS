"""Check the actual waveform/native compile flags against the requested profile."""
import json
import os
from pathlib import Path
import shlex
import sys

KEYS = (
    "RICKYOS_PLUS_UI_WHITE_MAINTENANCE",
    "RICKYOS_PLUS_READER_BLACK_RESTORE_PHASES",
    "RICKYOS_PLUS_READER_RETAINED_BLACK_PULSE",
    "RICKYOS_PLUS_READER_QUALITY",
    "RICKYOS_PLUS_READER_EARLY_ERASE",
    "RICKYOS_PLUS_READER_FINE_RIPPLE",
    "RICKYOS_PLUS_READER_MOTION_PERIOD_US",
    "RICKYOS_PLUS_READER_RIPPLE_SPREAD",
    "RICKYOS_PLUS_READER_SWEEP_SPREAD",
    "RICKYOS_PLUS_READER_ERASE_OVERLAP_PHASES",
    "RICKYOS_PLUS_READER_RIPPLE_OVERLAP_PHASES",
)


def check(database, expected):
    found = set()
    for entry in database:
        source = Path(entry["file"])
        if "/components/metalio_board/display/" not in source.as_posix() or source.name not in ("my_waveform.c", "render.c"):
            continue
        found.add(source.name)
        arguments = entry.get("arguments") or shlex.split(entry["command"])
        definitions = {}
        for argument in arguments:
            if argument.startswith("-D"):
                key, _, value = argument[2:].partition("=")
                definitions.setdefault(key, set()).add(value or "1")
        for key, value in expected.items():
            if definitions.get(key) != {value}:
                raise ValueError(f"{source.name}: {key} expected {value}, got {definitions.get(key)}")
    if found != {"my_waveform.c", "render.c"}:
        raise ValueError(f"Missing production display compile units: {found}")


def main():
    missing = [key for key in KEYS if key not in os.environ]
    if missing:
        raise ValueError(f"Display profile must be explicit before checking: {missing}")
    expected = {key: os.environ[key] for key in KEYS}
    path = Path(sys.argv[1])
    check(json.loads(path.read_text()), expected)
    path.with_name("display-profile.json").write_text(json.dumps(expected, indent=2) + "\n")
    print("Plus display compile profile verified: " + ", ".join(f"{k}={v}" for k, v in expected.items()))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError) as error:
        raise SystemExit(str(error)) from error
