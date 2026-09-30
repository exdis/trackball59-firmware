#!/usr/bin/env python3
"""Convert this board's pre-dropdown Vial export without modifying its backup."""

import argparse
import hashlib
import json
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, help="New file; never overwrites")
    args = parser.parse_args()
    original = args.source.read_bytes()
    data = json.loads(original)
    if data.get("version") != 1 or data.get("uid") != 12140365147794154861:
        parser.error("Expected this Trackball59's version-1 Vial export")
    print(f"Original layout_options: {data.get('layout_options')!r}")
    print(f"Original SHA-256: {hashlib.sha256(original).hexdigest()}")
    if data.get("layout_options") != -1:
        parser.error("Expected the old no-layout-options sentinel (-1)")
    try:
        struct.pack(">BBI", 3, 2, data["layout_options"])
    except struct.error as error:
        print(f"Reproduced Vial encoding failure: {error}")
    else:
        raise AssertionError("Expected unsigned packing to reject -1")
    data["layout_options"] = 0  # New dropdown: Off, until selected in Vial.
    struct.pack(">BBI", 3, 2, data["layout_options"])
    encoded = (json.dumps(data, ensure_ascii=True) + "\n").encode("utf-8")
    restored = json.loads(encoded)
    before = json.loads(original)
    changed = [key for key in before if before[key] != restored[key]]
    assert set(before) == set(restored) and changed == ["layout_options"]
    if args.output:
        with args.output.open("xb") as output:
            output.write(encoded)
        assert json.loads(args.output.read_bytes()) == restored
        print(f"Created: {args.output}")
    assert args.source.read_bytes() == original
    print("Validated: only layout_options changes (-1 -> 0); original untouched")


if __name__ == "__main__":
    main()
