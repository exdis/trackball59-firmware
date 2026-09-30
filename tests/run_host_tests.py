#!/usr/bin/env python3
"""Compile verbatim firmware excerpts, then check baseline Vial compatibility."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
KEYMAP = "trackballseries/59ctc/keymaps/vial/"
BASELINE = "be018da0675741cf2c6aceaa0d56feff17f7b292"


def source(path):
    return (ROOT / path).read_text()


def baseline(path):
    return subprocess.check_output(
        ["git", "show", f"{BASELINE}:{path}"], cwd=ROOT, text=True
    )


def excerpt(text, start, end, path):
    """Fail closed when a unique boundary changes; retain source diagnostics."""
    assert text.count(start) == 1, (path, start)
    assert text.count(end) == 1, (path, end)
    a, b = text.index(start), text.index(end)
    assert a < b
    return f'#line {text[:a].count(chr(10)) + 1} "{path}"\n' + text[a:b]


def function(text, name, path):
    matches = list(re.finditer(
        r"^(?:static )?\w+ " + re.escape(name) + r"\([^;{}]*\)\s*\{", text, re.M
    ))
    assert len(matches) == 1, (path, name)
    start = matches[0].start()
    depth = 0
    # Ignore braces in comments and literals, not merely lines or indentation.
    tokens = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]'
    for token in re.finditer(tokens, text[matches[0].end() - 1:], re.S):
        if token[0] == "{":
            depth += 1
        elif token[0] == "}":
            depth -= 1
            if depth == 0:
                end = matches[0].end() - 1 + token.end()
                return f'#line {text[:start].count(chr(10)) + 1} "{path}"\n' + text[start:end] + "\n"
    raise AssertionError(f"Unclosed function: {name}")


def translation_unit():
    shared_path = "trackballseries/trackballseries.c"
    keymap_path = KEYMAP + "keymap.c"
    shared, keymap = source(shared_path), source(keymap_path)
    shared = excerpt(shared, "#ifdef POINTING_DEVICE_ENABLE\n", "#if defined(KEYBOARD_bastardkb", shared_path)
    prefix = excerpt(keymap, "#ifdef POINTING_DEVICE_ENABLE\nstatic uint8_t dragscroll_layer;", "// clang-format off", keymap_path)
    # Only timer.h is included in this prefix. Hardware timing is outside scope.
    prefix = re.sub(r'^#\s*include "timer.h"', '// timer.h omitted by host extraction', prefix, flags=re.M)
    return '\n'.join([
        '#define POINTING_DEVICE_ENABLE', '#define VIA_ENABLE', '#define VIAL_ENABLE',
        f'#include "{ROOT / (KEYMAP + "config.h")}"',
        f'#include "{ROOT / "trackballseries/trackballseries.h"}"',
        shared, prefix,
        function(keymap, "layer_state_set_user", keymap_path),
        function(keymap, "process_record_user", keymap_path),
        f'#include "{TESTS / "host_dragscroll.c"}"',
    ])


class CompatibilityTests(unittest.TestCase):
    def test_selector_and_unchanged_definition(self):
        current = json.loads(source(KEYMAP + "vial.json"))
        old = json.loads(baseline(KEYMAP + "vial.json"))
        self.assertEqual(current["layouts"].pop("labels"), [
            ["Drag-scroll layer", "Off"] + [f"Layer {n}" for n in range(1, 10)]
        ])
        # Includes matrix, full physical layout, VID/PID, and custom-keycode order.
        self.assertEqual(current, old)
        keys = [key for row in current["layouts"]["keymap"] for key in row if isinstance(key, str)]
        positions = [key for key in keys if "\n" not in key]
        encoders = [key.split("\n") for key in keys if "\n" in key]
        self.assertEqual(len(positions), 61)
        self.assertEqual(len(set(positions)), 61)
        self.assertEqual({parts[0] for parts in encoders}, {"0,0", "0,1", "1,0", "1,1"})
        self.assertEqual(len(encoders), 4)
        for parts in encoders:
            self.assertEqual(parts[1:], [""] * 8 + ["e"])
        firmware_layout = json.loads(source("trackballseries/59ctc/info.json"))["layouts"]["LAYOUT"]["layout"]
        self.assertEqual({tuple(map(int, key.split(","))) for key in positions},
                         {tuple(key["matrix"]) for key in firmware_layout})
        for position in positions:
            row, col = map(int, position.split(","))
            self.assertTrue(0 <= row < 10 and 0 <= col < 7)
        self.assertLessEqual((10 - 1).bit_length(), 8)  # Fits VIA's existing one-byte field.

    def test_uid_and_export_dimensions(self):
        config = source(KEYMAP + "config.h")
        old = baseline(KEYMAP + "config.h")
        for name in ("VIAL_KEYBOARD_UID", "DYNAMIC_KEYMAP_LAYER_COUNT", "DYNAMIC_KEYMAP_MACRO_COUNT"):
            pattern = r"^#define\s+" + name + r"\s+([^\n]+)"
            self.assertEqual(re.search(pattern, config, re.M)[1], re.search(pattern, old, re.M)[1])
        uid = re.search(r"VIAL_KEYBOARD_UID\s+\{([^}]+)\}", config)[1]
        self.assertEqual(bytes(int(x.strip(), 0) for x in uid.split(",")).hex(), "6da5cd8dc73d7ba8")
        self.assertRegex(config, r"#define DYNAMIC_KEYMAP_LAYER_COUNT 10\b")
        self.assertRegex(config, r"#define DYNAMIC_KEYMAP_MACRO_COUNT 32\b")
        # No export addressing changes: matrix geometry, physical positions,
        # keyboard-specific keycode numbers, or EEPROM layout configuration.
        for path in ("trackballseries/59ctc/info.json", "trackballseries/info.json",
                     "trackballseries/config.h", "trackballseries/59ctc/config.h"):
            self.assertEqual(source(path), baseline(path), path)
        header = "trackballseries/trackballseries.h"
        enum = r"enum charybdis_keycodes\s*\{.*?\};"
        self.assertEqual(re.search(enum, source(header), re.S)[0], re.search(enum, baseline(header), re.S)[0])
        board = "trackballseries/59ctc/v2/splinky/config.h"
        self.assertEqual(source(board), baseline(board).replace("keymaps/via/glcdfont.c", "keymaps/vial/glcdfont.c"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "/usr/bin/clang"))
    parser.add_argument("--build-parent", type=Path, default=TESTS)
    args = parser.parse_args()
    if not args.build_parent.is_dir():
        parser.error("--build-parent must be an existing directory")
    with tempfile.TemporaryDirectory(prefix="host-dragscroll-", dir=args.build_parent) as directory:
        build = Path(directory)
        unit = build / "firmware.c"
        unit.write_text(translation_unit())
        for optimization in ("-O0", "-O2"):
            executable = build / "host-tests"
            command = [args.cc, "-std=gnu11", optimization, "-g", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-parameter", "-Wno-unused-variable", "-fsanitize=address,undefined",
                       "-fno-sanitize-recover=all", "-I", str(TESTS), str(unit), "-o", str(executable)]
            print("+ " + " ".join(command), flush=True)
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(CompatibilityTests)
    )
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
