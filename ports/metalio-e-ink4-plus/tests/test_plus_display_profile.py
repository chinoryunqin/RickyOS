"""Reject omitted/mismatched display options in real compiler command records."""
import importlib.util
from pathlib import Path
import unittest

PORT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("display_profile", PORT / "scripts/check_plus_display_profile.py")
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class DisplayProfileTest(unittest.TestCase):
    expected = {"RICKYOS_PLUS_READER_QUALITY": "1", "RICKYOS_PLUS_READER_ERASE_OVERLAP_PHASES": "0"}

    def database(self):
        flags = ["cc", "-DRICKYOS_PLUS_READER_QUALITY=1", "-DRICKYOS_PLUS_READER_ERASE_OVERLAP_PHASES=0"]
        return [
            {"file": "/project/components/metalio_board/display/waveforms/my_waveform.c", "arguments": flags.copy()},
            {"file": "/project/components/metalio_board/display/render.c", "command": " ".join(flags)},
        ]

    def test_actual_sources_and_both_command_formats(self):
        profile.check(self.database(), self.expected)

    def test_missing_or_conflicting_define(self):
        for change in ("missing", "wrong", "conflicting"):
            with self.subTest(change=change):
                db = self.database()
                args = db[0]["arguments"]
                if change == "missing":
                    args.pop(1)
                elif change == "wrong":
                    args[1] = "-DRICKYOS_PLUS_READER_QUALITY=0"
                else:
                    args.append("-DRICKYOS_PLUS_READER_QUALITY=0")
                with self.assertRaises(ValueError):
                    profile.check(db, self.expected)

    def test_native_feed_is_checked_too(self):
        db = self.database()
        db[1]["command"] = "cc -DRICKYOS_PLUS_READER_QUALITY=1"
        with self.assertRaises(ValueError):
            profile.check(db, self.expected)

    def test_unrelated_render_file_cannot_satisfy_check(self):
        db = self.database()
        db[1]["file"] = "/project/other/render.c"
        with self.assertRaises(ValueError):
            profile.check(db, self.expected)


if __name__ == "__main__":
    unittest.main()
