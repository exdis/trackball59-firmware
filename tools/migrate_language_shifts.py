#!/usr/bin/env python3
"""Prepare this user's Shift Mod-Tap layout; never change the source export."""

import argparse
import copy
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    original = args.source.read_bytes()
    data = json.loads(original)
    if data.get("uid") != 12140365147794154861 or data.get("version") != 1:
        parser.error("Expected this Trackball59's Vial export")
    if not isinstance(data.get("layout_options"), int) or not 0 <= data["layout_options"] <= 9:
        parser.error("Export the current scroll-layer firmware's layout first")
    dances = data["tap_dance"]
    if dances[0][:2] != ["M0", "KC_LSHIFT"] or dances[1][:2] != ["M1", "KC_RSHIFT"]:
        parser.error("TD0/TD1 do not match the expected language-tap/Shift-hold actions")
    # Only these two known physical Shift positions, not every use of a dance.
    replacements = [(0, 3, 0, "TD(0)", "LSFT_T(KC_F23)"),
                    (0, 8, 0, "TD(1)", "RSFT_T(KC_F24)")]
    changed = copy.deepcopy(data)
    for layer, row, col, old, new in replacements:
        if data["layout"][layer][row][col] != old:
            parser.error(f"Expected {old} at {layer},{row},{col}")
        changed["layout"][layer][row][col] = new
    # Current synchronous playback supports balanced basic chords, not recursive
    # QMK/macro actions. Reject payloads outside this deliberately small scope.
    allowed = {"KC_LCTRL", "KC_LSHIFT", "KC_LGUI", "KC_LALT", "KC_BSLASH"}
    for index, macro in enumerate(data["macro"][:2]):
        if not macro:
            parser.error(f"M{index} is empty")
        held = set()
        for action in macro:
            kind, *value = action
            if kind == "delay" and len(value) == 1 and isinstance(value[0], int) and 0 <= value[0] <= 200:
                continue
            if kind not in ("down", "up", "tap") or not isinstance(value, list) or not set(value) <= allowed:
                parser.error(f"Review M{index}: unsupported action {kind!r}")
            for key in value:
                if kind == "down":
                    if key in held:
                        parser.error(f"M{index} presses {key} twice")
                    held.add(key)
                elif kind == "up":
                    if key not in held:
                        parser.error(f"M{index} releases unheld {key}")
                    held.remove(key)
        if held:
            parser.error(f"M{index} leaves keys held")
    encoded = (json.dumps(changed, ensure_ascii=True) + "\n").encode()
    check = json.loads(encoded)
    for layer, row, col, old, new in replacements:
        assert check["layout"][layer][row][col] == new
        check["layout"][layer][row][col] = old
    assert check == data
    with args.output.open("xb") as output:
        output.write(encoded)
    assert args.source.read_bytes() == original
    print(f"Source SHA-256: {hashlib.sha256(original).hexdigest()}")
    print(f"Created: {args.output}")
    print(f"Preserved scroll-layer choice: {data['layout_options']}")
    print("Validated: only two base-layer Shift assignments changed; source untouched")


if __name__ == "__main__":
    main()
