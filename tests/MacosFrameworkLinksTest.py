#!/usr/bin/env python3
"""Run one real dylib graph from both app and nested-helper executable paths."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts/macos_normalize_framework_links.py"


class FrameworkLinksTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="owl framework links ")
        self.addCleanup(self.temp.cleanup)
        root = pathlib.Path(self.temp.name)
        self.app = root / "Fixture.app"
        self.frameworks = self.app / "Contents/Frameworks"
        self.frameworks.mkdir(parents=True)
        source = root / "source.c"
        source.write_text("int leaf(void) { return 7; }\n")
        self.leaf = self.frameworks / "libleaf.dylib"
        self.run_ok("clang", "-dynamiclib", str(source), "-o", str(self.leaf),
                    "-Wl,-install_name,@executable_path/../Frameworks/libleaf.dylib")
        source.write_text("extern int leaf(void); int shared(void) { return leaf(); }\n")
        self.shared = self.frameworks / "libshared.dylib"
        self.run_ok("clang", "-dynamiclib", str(source), str(self.leaf), "-o", str(self.shared),
                    "-Wl,-install_name,@rpath/libshared.dylib", "-Wl,-headerpad_max_install_names")
        source.write_text("extern int shared(void); int main(void) { return shared() == 7 ? 0 : 1; }\n")
        self.executables = []
        for folder, rpath in [("MacOS", "../Frameworks"), ("Resources/bin", "../../Frameworks")]:
            executable = self.app / "Contents" / folder / "fixture"
            executable.parent.mkdir(parents=True)
            self.run_ok("clang", str(source), str(self.shared), "-o", str(executable),
                        "-Wl,-rpath,@loader_path/" + rpath)
            self.executables.append(executable)

    def run_ok(self, *args):
        return subprocess.run(args, check=True, capture_output=True, text=True)

    def test_app_and_nested_helper_share_the_same_libraries(self):
        self.run_ok(str(self.executables[0]))
        failed = subprocess.run([str(self.executables[1])], capture_output=True, text=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("Library not loaded", failed.stderr)
        self.run_ok(sys.executable, str(SCRIPT), str(self.app))
        self.run_ok("codesign", "--force", "--sign", "-", str(self.shared))
        for executable in self.executables:
            self.run_ok(str(executable))
        self.run_ok(sys.executable, str(SCRIPT), str(self.app), "--check")

    def test_missing_target_fails_without_editing_the_library(self):
        before = self.shared.read_bytes()
        self.leaf.unlink()
        failed = subprocess.run([sys.executable, str(SCRIPT), str(self.app)], capture_output=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(self.shared.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
