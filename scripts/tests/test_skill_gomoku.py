"""Compile the real rules/AI with sanitizers. Never reads a device or SD card."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SkillGomokuTests(unittest.TestCase):
    def test_store(self):
        with tempfile.TemporaryDirectory(prefix="rickyos-skill-save-") as temporary:
            binary = Path(temporary) / "test"
            source = ROOT / "src/activities/apps/gomoku"
            subprocess.run(["clang++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(source),
                            "-I", str(Path(__file__).with_name("skill_gomoku_fake")),
                            str(source / "SkillGomoku.cpp"), str(source / "SkillGomokuStore.cpp"),
                            str(Path(__file__).with_name("skill_gomoku_store_native.cpp")),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)

    def test_native(self):
        with tempfile.TemporaryDirectory(prefix="rickyos-skill-gomoku-") as temporary:
            binary = Path(temporary) / "test"
            source = ROOT / "src/activities/apps/gomoku"
            subprocess.run(["clang++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(source),
                            str(source / "SkillGomoku.cpp"), str(Path(__file__).with_name("skill_gomoku_native.cpp")),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=60)


if __name__ == "__main__":
    unittest.main()
