"""Compare the production full-plane helper with the previous strip traversal."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ReaderGrayPlaneTest(unittest.TestCase):
    def test_planes_match_strip_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "gray-plane"
            subprocess.run([
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(ROOT / "src/activities/reader"),
                str(ROOT / "test/metalio_refresh/test_gray_plane.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
