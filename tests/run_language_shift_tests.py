#!/usr/bin/env python3
"""Build and run production language-shift C with the native QMK tapping harness."""

import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmk", required=True, type=Path)
    parser.add_argument("--build-parent", required=True, type=Path)
    parser.add_argument("--cc", default="/usr/bin/clang")
    args = parser.parse_args()
    qmk = args.qmk.resolve()
    if not args.build_parent.is_dir():
        parser.error("--build-parent must exist")
    if not (qmk / "lib/googletest/googletest/src/gtest-all.cc").is_file():
        parser.error("initialize QMK lib/googletest first (git submodule update --init --depth 1 lib/googletest)")
    fixture = ROOT / "tests/qmk_language_shift"
    link = qmk / "tests/trackball_language_shift"
    if not (qmk / "tests").is_dir():
        parser.error("--qmk must contain the upstream tests directory")
    if link.is_symlink():
        if link.resolve() != fixture:
            parser.error(f"refusing to replace {link}")
    elif link.exists():
        parser.error(f"refusing to replace {link}")
    else:
        link.symlink_to(fixture, target_is_directory=True)
    # An isolated build also isolates gtest objects, flags, and QMK error files.
    with tempfile.TemporaryDirectory(prefix="language-shift-", dir=args.build_parent.resolve()) as build:
        command = [
            "make", "-f", "builddefs/build_test.mk", "-j4", "SILENT=false",
            "TEST=trackball_language_shift", "TEST_OUTPUT=trackball_language_shift",
            "FULL_TESTS=trackball_language_shift", "TEST_PATH=tests/trackball_language_shift",
            f"TRACKBALL_ROOT={ROOT}", f"BUILD_DIR={build}", f"CC={args.cc}",
            "CXXSTANDARD=-std=gnu++17", "LDFLAGS=-lc++ -lpthread",
            "EXTRAFLAGS=-Wno-strict-prototypes -Wno-include-next-absolute-path -Wno-c++11-narrowing",
        ]
        print("+ " + " ".join(command), flush=True)
        subprocess.run(command, cwd=qmk, check=True)
        subprocess.run([str(Path(build) / "test/trackball_language_shift.elf")], cwd=qmk, check=True)


if __name__ == "__main__":
    main()
