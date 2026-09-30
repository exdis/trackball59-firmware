#!/usr/bin/env python3
"""Read-only RP2040 UF2/ELF inspection; only --report keeps a generated file.

Uses Python's standard library. Historical SROM downloads require explicit opt-in;
they are parsed in memory, never executed or saved as firmware/source files.
Optional objdump inspection uses a disposable system-temporary payload copy.
All address ranges in the JSON report are half-open. No device access is used.
"""

import argparse
from collections import Counter
import hashlib
import itertools
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import urllib.request


FLASH = 0x10000000
FAMILY = 0xE48BFF56
REPO = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def ranges(addresses):
    result = []
    for address in sorted(addresses):
        if result and address == result[-1][1]:
            result[-1][1] += 1
        else:
            result.append([address, address + 1])
    return [{"start": hex(a), "end": hex(b), "bytes": b - a} for a, b in result]


def parse_uf2(raw):
    if not raw or len(raw) % 512:
        raise ValueError("UF2 length must be a nonzero multiple of 512")
    blocks = []
    numbers = set()
    count = len(raw) // 512
    for offset in range(0, len(raw), 512):
        m0, m1, flags, address, size, number, total, family = struct.unpack_from("<8I", raw, offset)
        end_magic = struct.unpack_from("<I", raw, offset + 508)[0]
        if (m0, m1, end_magic) != (0x0A324655, 0x9E5D5157, 0x0AB16F30):
            raise ValueError(f"Invalid UF2 magic in block at file offset {offset}")
        # These images must be plain family-tagged flash blocks, not file/MD5/extension blocks.
        if flags != 0x2000 or family != FAMILY:
            raise ValueError(f"Unexpected flags/family at {offset}: {flags:#x}/{family:#x}")
        if not 0 < size <= 476 or size % 4 or address % 4:
            raise ValueError(f"Invalid payload size/alignment at {offset}")
        if not FLASH <= address < address + size <= FLASH + 16 * 1024 * 1024:
            raise ValueError(f"Payload outside RP2040 XIP flash window at {offset}")
        if total != count or number >= total or number in numbers:
            raise ValueError(f"Invalid total, duplicate or out-of-range block number at {offset}")
        numbers.add(number)
        blocks.append((address, raw[offset + 32:offset + 32 + size]))
    blocks.sort()
    segments = []
    for address, payload in blocks:
        if segments and address < segments[-1][0] + len(segments[-1][1]):
            raise ValueError(f"Overlapping flash payload at {address:#x}")
        if segments and address == segments[-1][0] + len(segments[-1][1]):
            segments[-1][1].extend(payload)
        else:
            segments.append([address, bytearray(payload)])
    segments = [(a, bytes(b)) for a, b in segments]
    summary = {
        "file_bytes": len(raw), "sha256": sha(raw), "blocks": count,
        "validation": "All block magics, flags, RP2040 family IDs, sizes, numbering and addresses valid; no overlaps",
        "flags": "0x2000", "family": hex(FAMILY),
        "payload_sizes": dict(Counter(len(b) for _, b in blocks)),
        "payload_bytes": sum(len(b) for _, b in segments),
        "segments": [{"start": hex(a), "end": hex(a + len(b)), "bytes": len(b),
                      "sha256": sha(b)} for a, b in segments],
        "coverage_note": "Image coverage is not a measurement of physical flash capacity; gaps are not filled",
    }
    return segments, summary


def extract(segments, address, size):
    for start, data in segments:
        if start <= address and address + size <= start + len(data):
            return data[address - start:address - start + size]
    return None


def occurrences(segments, needle):
    result = []
    for address, data in segments:
        start = 0
        while (offset := data.find(needle, start)) >= 0:
            result.append(hex(address + offset))
            start = offset + 1
    return result


def boot_crc(data):
    # RP2040 boot ROM uses non-reflected CRC-32, seed 0xffffffff, no final XOR.
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
    return crc


def immediate_uid_candidates(segments):
    """Recognize Thumb byte-store chains used by older Vial ID responses."""
    candidates = []
    for address, data in segments:
        for offset in range(0, len(data) - 2, 2):
            if data[offset + 1] != 0x23:  # movs r3, #imm8
                continue
            value, stores = 0, {}
            for pos in range(offset, min(offset + 80, len(data) - 1), 2):
                op = struct.unpack_from("<H", data, pos)[0]
                if op & 0xFF00 == 0x2300:
                    value = op & 255
                elif op & 0xFF00 == 0x3300:
                    value += op & 255
                elif op & 0xFF00 == 0x3B00:
                    value -= op & 255
                elif op & 0xF83F == 0x7023:  # strb r3, [r4, #imm5]
                    stores[(op >> 6) & 31] = value & 255
                else:
                    break
                if all(i in stores for i in range(13)):
                    candidates.append({"address": hex(address + offset),
                                       "protocol": int.from_bytes(bytes(stores[i] for i in range(4)), "little"),
                                       "uid": bytes(stores[i] for i in range(4, 12)).hex(),
                                       "vialrgb_flag": stores[12]})
                    break
    return candidates


def c_array(text, name):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    match = re.search(r"\b" + re.escape(name) + r"\s*\[[^\]]*\][^=;]*=\s*\{([^}]+)\}", text)
    if not match:
        return None
    tokens = [t.strip() for t in match[1].split(",") if t.strip()]
    return bytes(int(t, 0) for t in tokens)


def parse_elf(path):
    raw = path.read_bytes()
    if raw[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected ELF32 little-endian")
    header = struct.unpack_from("<HHIIIIIHHHHHH", raw, 16)
    if header[1] != 40:
        raise ValueError("Expected ARM ELF")
    phoff, shoff = header[4:6]
    phsize, phnum, shsize, shnum, names_index = header[8:13]
    headers = [struct.unpack_from("<10I", raw, shoff + i * shsize) for i in range(shnum)]
    names_hdr = headers[names_index]
    names = raw[names_hdr[4]:names_hdr[4] + names_hdr[5]]
    sections, symbols = [], {}
    for section in headers:
        name, kind, flags, address, offset, size, link, info, align, entsize = section
        name = names[name:].split(b"\0", 1)[0].decode()
        sections.append({"name": name, "address": hex(address), "bytes": size,
                         "allocated": bool(flags & 2), "nobits": kind == 8})
        if kind == 2:
            strings_hdr = headers[link]
            strings = raw[strings_hdr[4]:strings_hdr[4] + strings_hdr[5]]
            for pos in range(offset, offset + size, entsize):
                n, value, length, sym_info, other, index = struct.unpack_from("<IIIBBH", raw, pos)
                sym_name = strings[n:].split(b"\0", 1)[0].decode()
                if sym_name:
                    symbols[sym_name] = {"value": value, "size": length, "binding": sym_info >> 4}
    loads = []
    for i in range(phnum):
        kind, offset, vaddr, paddr, size, memsize, flags, align = struct.unpack_from("<8I", raw, phoff + i * phsize)
        if kind == 1 and size and FLASH <= paddr < FLASH + 16 * 1024 * 1024:
            loads.append((paddr, raw[offset:offset + size]))
    return raw, sections, symbols, loads


def inspect(args):
    report = {"scope": "Static file inspection only; no USB operations or flashing. Ranges are half-open."}
    images, report["images"] = {}, {}
    for label in ("left", "right", "baseline"):
        path = getattr(args, label)
        segments, summary = parse_uf2(path.read_bytes())
        summary["path"] = str(path)
        boot = extract(segments, FLASH, 256)
        if boot is None:
            raise ValueError(f"{label}: boot2 not fully covered")
        stored = struct.unpack_from("<I", boot, 252)[0]
        summary["boot2"] = {"sha256": sha(boot), "hex": boot.hex(),
                            "stored_crc": hex(stored), "calculated_crc": hex(boot_crc(boot[:252])),
                            "crc_valid": stored == boot_crc(boot[:252])}
        strings = []
        for address, data in segments:
            for encoding, pattern in (("ascii", rb"[\x20-\x7e]{6,}"),
                                      ("utf-16le", rb"(?:[\x20-\x7e]\x00){4,}")):
                for match in re.finditer(pattern, data):
                    text = match[0].decode(encoding)
                    if re.search(r"qmk|vial|trackball|splinky|pmw|chibios|20\d\d[-/]", text, re.I):
                        strings.append({"address": hex(address + match.start()), "encoding": encoding, "text": text})
        summary["firmware_strings"] = strings
        images[label] = segments
        report["images"][label] = summary
    report["comparisons"] = []
    for a, b in itertools.combinations(images, 2):
        different, common, same = [], 0, 0
        for start_a, data_a in images[a]:
            for start_b, data_b in images[b]:
                start, end = max(start_a, start_b), min(start_a + len(data_a), start_b + len(data_b))
                if start >= end:
                    continue
                common += end - start
                for offset, (x, y) in enumerate(zip(data_a[start-start_a:end-start_a], data_b[start-start_b:end-start_b])):
                    if x != y:
                        different.append(start + offset)
                    else:
                        same += 1
        boots = [extract(images[label], FLASH, 256) for label in (a, b)]
        diff_ranges = ranges(different)
        report["comparisons"].append({
            "a": a, "b": b, "common_bytes": common, "equal_bytes": same,
            "different_bytes": len(different), "difference_range_count": len(diff_ranges),
            "difference_span": [hex(different[0]), hex(different[-1] + 1)] if different else None,
            "first_difference_ranges": diff_ranges[:32],
            "boot2_identical": boots[0] == boots[1],
            "boot2_differing_offsets": [i for i in range(256) if boots[0][i] != boots[1][i]],
        })
    uid_text = args.uid_config.read_text()
    uid_match = re.search(r"#define\s+VIAL_KEYBOARD_UID\s*\{([^}]+)\}", uid_text)
    if not uid_match:
        raise ValueError("VIAL_KEYBOARD_UID not found")
    uid = bytes(int(t.strip(), 0) for t in uid_match[1].split(","))
    report["vial_uid"] = {"source": str(args.uid_config), "hex": uid.hex(),
                          "matches": {label: occurrences(data, uid) for label, data in images.items()},
                          "word_matches": {label: [occurrences(data, uid[i:i+4]) for i in (0, 4)]
                                           for label, data in images.items()},
                          "immediate_store_candidates": {label: immediate_uid_candidates(data)
                                                         for label, data in images.items()},
                          "note": "UID is a firmware model identifier, not a unique hardware serial; absence of contiguous bytes is inconclusive"}
    report["srom_references"] = []
    for sensor in ("pmw3389", "pmw3360"):
        path = args.upstream / f"drivers/sensors/{sensor}.c"
        sources = [(str(path), path.read_bytes())]
        if args.historical_srom:
            url = f"https://raw.githubusercontent.com/qmk/qmk_firmware/0.22.14/drivers/sensors/{sensor}.c"
            with urllib.request.urlopen(url, timeout=30) as response:
                sources.append((url, response.read()))
        for source, raw in sources:
            blob = c_array(raw.decode(), "pmw33xx_firmware_data")
            item = {"sensor": sensor, "source": source, "source_sha256": sha(raw), "array_present": blob is not None}
            if blob is not None:
                item.update({"bytes": len(blob), "sha256": sha(blob),
                             "full_matches": {label: occurrences(data, blob) for label, data in images.items()},
                             "prefix32_matches": {label: occurrences(data, blob[:32]) for label, data in images.items()}})
            report["srom_references"].append(item)
    raw, sections, symbols, loads = parse_elf(args.elf)
    chosen = {name: {**s, "value": hex(s["value"])} for name, s in symbols.items()
              if re.search(r"__flash|__heap|__bss|__data|stack.*__|FLASH_LEN|srom|firmware_data|wear_leveling$|BOOT2_ROM$", name)}
    allocated = [s for s in sections if s["allocated"] and s["bytes"]]
    report["baseline_elf"] = {
        "path": str(args.elf), "sha256": sha(raw), "allocated_sections": allocated, "symbols": chosen,
        "uf2_load_segments": [{"address": hex(a), "bytes": len(b),
                               "matches": extract(images["baseline"], a, len(b)) == b} for a, b in loads],
        "allocated_nobits_total": sum(s["bytes"] for s in allocated if s["nobits"]),
        "ram_note": "size's bss column sums allocated NOBITS, including .heap, stacks and .ram sections; it is not just the .bss variable section",
    }
    end = symbols["__flash_binary_end"]["value"]
    flash_end = symbols["__flash1_end__"]["value"]
    report["storage"] = {"baseline_binary_end": hex(end), "baseline_binary_bytes": end - FLASH,
                         "linker_flash_end": hex(flash_end), "linker_flash_bytes": flash_end - FLASH,
                         "note": "Linker flash limit is configured, not probed hardware capacity. Backup UF2s do not carry ELF symbol tables."}
    # The linked erase routine must be reviewed before treating this source-default region as confirmed.
    backing_start = FLASH + 2 * 1024 * 1024 - 8192
    report["storage"]["source_default_2MiB_8KiB_region"] = {
        "start": hex(backing_start), "end": hex(FLASH + 2 * 1024 * 1024),
        "logical_bytes": 4096, "baseline_binary_overlaps": end > backing_start,
        "images": {},
    }
    for label, data in images.items():
        region = extract(data, backing_start, 8192)
        report["storage"]["source_default_2MiB_8KiB_region"]["images"][label] = (
            {"covered": False} if region is None else
            {"covered": True, "sha256": sha(region), "non_ff_bytes": sum(b != 255 for b in region),
             "non_ff_ranges": ranges(backing_start + i for i, b in enumerate(region) if b != 255)})
    report["storage"]["last_8KiB_of_1MiB_coverage"] = {}
    for label, data in images.items():
        region = extract(data, FLASH + 1024 * 1024 - 8192, 8192)
        report["storage"]["last_8KiB_of_1MiB_coverage"][label] = (
            {"covered": False} if region is None else
            {"covered": True, "sha256": sha(region), "non_ff_bytes": sum(b != 255 for b in region)})
    report["storage"]["address_literal_candidates"] = {
        label: {hex(value): occurrences(data, struct.pack("<I", value))
                for value in (0x100FE000, 0x101FE000, 0x000FE000, 0x001FE000, 0x100FF000, 0x000FF000)}
        for label, data in images.items()
    }
    if args.objdump:
        report["baseline_disassembly"] = {}
        for name in ("pmw33xx_srom_get_length", "pmw33xx_get_cpi", "backing_store_erase", "backing_store_read_bulk"):
            output = subprocess.check_output([args.objdump, "-d", f"--disassemble={name}", str(args.elf)], text=True)
            report["baseline_disassembly"][name] = output.splitlines()
        # Disassemble a generated temporary payload copy, never a device or the backup itself.
        with tempfile.TemporaryDirectory(prefix="uf2-inspection-") as directory:
            payload = Path(directory) / "left.bin"
            payload.write_bytes(images["left"][0][1])
            output = subprocess.check_output([
                args.objdump, "-D", "-b", "binary", "-m", "arm", "-M", "force-thumb",
                f"--adjust-vma={FLASH}", str(payload)], text=True)
            lines = output.splitlines()
            candidates = []
            for i, line in enumerate(lines):
                if (re.search(r"(?:movs|cmp)\s+r[0-7], #(14|15|50|100|109|165|205|141|199|61|123|168|253|254)\b", line)
                        or re.search(r"100fe000|000fe000", line)):
                    candidates.append("\n".join(lines[max(0, i-6):i+14]))
            report["backup_disassembly_candidates"] = {
                "note": "Heuristic instruction candidates, not automatic function identification; data can decode as instructions",
                "left": candidates,
            }
            report["backup_disassembly_windows"] = {}
            for window in args.left_window:
                start, end = (int(v, 0) for v in window.split(":"))
                report["backup_disassembly_windows"][window] = [
                    line for line in lines if (match := re.match(r"([0-9a-f]+):", line))
                    and start <= int(match[1], 16) < end]
    report["limitations"] = [
        "A full SROM match identifies embedded sensor firmware, not the installed physical sensor or successful runtime use.",
        "Absence of a reference SROM is not proof of another sensor; firmware may omit or use a different SROM.",
        "Different boot2 bytes require compatibility review, but do not alone prove incompatible flash hardware.",
        "No sensor registers, flash JEDEC ID, or physical flash capacity were read.",
        "Backing-store contents and firmware layout may differ between versions; matching Vial UID does not prove EEPROM layout compatibility.",
        "Data near 0x100fe000 may reflect 1MiB flash aliasing of configured 0x101fe000, but static images cannot prove aliasing or actual chip capacity.",
    ]
    if (report["images"]["left"]["sha256"] == "c30ac5ec1cb4fd2df988d05baa4ce12d2ddec51268499ab8ec08f3245da89dea"
            and report["images"]["baseline"]["sha256"] == "1821f5ff241c4a513ef20a78162e8c614e0f2f7b272bcabb218168d14dfd5605"):
        report["reviewed_findings_for_these_hashes"] = [
            "Backup CPI routines at 0x10006b60 and 0x10006b90 match PMW3360 behavior: Config1 register 0x0f, read/write-back workaround, 100-CPI steps and 12000-CPI limit. This is strong firmware-driver evidence, not a physical sensor identification.",
            "Baseline CPI routine at 0x10004e30 uses PMW3389 registers 0x0f/0x0e and 50-CPI steps. Sensor-driver behavior differs from the backups.",
            "Backup SROM length/byte hooks at 0x10006bde/0x10006be2 both return zero. Baseline SROM length hook also returns zero. Neither historical 4094-byte SROM reference is found when --historical-srom is used.",
            "Backup Vial ID response at 0x10009b9c constructs UID 6da5cd8dc73d7ba8 through immediate additions/subtractions and stores; contiguous-byte search alone misses it. The UID matches baseline.",
            "Backup erase routine at 0x10007560 computes offset 255<<13 = 0x1fe000 and length 128<<6 = 8192. Read routine at 0x100075c4 uses literal 0x101fe000 at 0x100075e0. Baseline has the same configured storage location and size.",
            "The backups cover only [0x10000000,0x10100000), not their configured [0x101fe000,0x10200000) backing store. Persistent-looking differences at 0x100fe000 are consistent with possible 1MiB flash aliasing, not proof. Do not assume a complete physical-flash or EEPROM backup on a 2MiB device.",
            "Baseline linker permits 2MiB flash, including the runtime EEPROM region; __flash_binary_end=0x1001718c is currently well below it. The linker limit itself does not reserve the final 8KiB from future firmware growth.",
            "Allocated NOBITS 264864 = .bss 12632 + .heap 245796 + stacks 6144 + .ram0 4 + .ram4 288. This is not 264864 bytes of static variables. Initialized .data is another 3712 bytes.",
        ]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--left", type=Path, required=True, help="Original left-half UF2 backup")
    parser.add_argument("--right", type=Path, required=True, help="Original right-half UF2 backup")
    parser.add_argument("--baseline", type=Path, required=True,
                        help="Compiled UF2 to compare with the backups (baseline or new candidate)")
    parser.add_argument("--elf", type=Path, required=True, help="ELF from the same build as --baseline")
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--uid-config", type=Path, default=REPO / "trackballseries/59ctc/keymaps/vial/config.h")
    parser.add_argument("--historical-srom", action="store_true", help="Fetch QMK 0.22.14 sensor C arrays over HTTPS")
    parser.add_argument("--objdump", help="Optional ARM objdump executable for linked-code evidence")
    parser.add_argument("--left-window", action="append", default=[], metavar="START:END",
                        help="Include a backup Thumb disassembly address window (requires --objdump)")
    parser.add_argument("--report", type=Path, help="New JSON report path; refuses to overwrite any existing file")
    args = parser.parse_args()
    report = inspect(args)
    text = json.dumps(report, indent=2) + "\n"
    if args.report:
        with args.report.open("x") as output:
            output.write(text)
        print(f"Generated report: {args.report}")
        for label, item in report["images"].items():
            print(f"{label}: {item['blocks']} valid blocks, {item['payload_bytes']} flash bytes, boot2 CRC valid={item['boot2']['crc_valid']}")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
