# Configurable Drag-Scroll

Status: both halves are flashed with the final language-shift firmware, which
includes configurable drag-scroll. The user confirmed all behavior working,
including scrolling and both Shifts. Use `firmware/language-shift.uf2` and
`layouts/current.vil`; see `LANGUAGE-SHIFT.md` for the final build provenance.

Both halves' backup address-range checks passed. All lower and upper reads
verified, and each half's ranges were byte-identical. The reports are private:
`~/Documents/trackball59-recovery/artifacts/right-flash-check/RESULTS.md` and
`~/Documents/trackball59-recovery/artifacts/left-flash-check/RESULTS.md`.

## Usage

In standard Vial or Vial Web, open Layouts and select Drag-scroll layer:
Off (default), or Layer 1 through Layer 9. Then assign MO(n) to a key.
Holding that key enables the layer and scrolling; releasing it removes that
layer's scrolling request. A selected layer below a higher active layer still
counts. Default-layer state also counts. Selecting Off disables only the
layer-driven request, not manual scrolling.

The choice is saved by Vial's existing layout-options mechanism, is included
in .vil exports, and applies immediately even if the selected layer is already
active. Importing the old layout can set this choice back to Off; choose the
scroll layer after importing.

Drg and DrgT keep their existing shared manual state. That manual state is ORed
with the selected layer: releasing Drg cannot cancel layer-driven scrolling,
and DrgT can be toggled while the layer is active. A manual toggle left on will
keep scrolling on after the layer is released. OLED DRG-T shows effective state.
Existing sniping behavior on layer 3 is retained.

Avoid layer 9 while automatic mouse-layer mode is enabled: the existing auto
layer timer uses cursor X/Y, which drag-scroll consumes. It can time out during
scrolling. This change does not redesign the automatic-layer feature.

### Importing The Original Export

The original export (`layouts/history/original.vil`, copied from
`~/Documents/keyboard_layout_tb59.vil`) contains layout_options=-1, Vial's
sentinel for a keyboard without layout choices. Once the new dropdown exists,
Vial tries to pack that value as an unsigned integer and fails before restoring
macros, tap dances, combos and QMK settings (keys/encoders may already be set).
This import compatibility case was missed by the initial host checks.

A migrated copy is available as `layouts/history/scroll-layer-compatible.vil`,
copied from `~/Documents/keyboard_layout_tb59-scroll-layer.vil`.
Only layout_options changed to 0 (Off); all other JSON values are identical,
and the original Documents export is untouched. The later working scroll-layer
export is `layouts/history/scroll-layer-working.vil`. For the current firmware,
prefer `layouts/current.vil`, which also includes the language/Shift assignments.
The conversion and unsigned-encoding regression check are reproducible with
`tools/migrate_layout.py`; it refuses to overwrite an existing output.

Refresh Vial Web and reconnect before importing the migrated copy: the failed
import leaves Vial's in-memory layout_options set to -1. After the complete
import succeeds, select the desired Drag-scroll layer in Layouts and export
a new layout under a different name.

## Compatibility

- Original manufacturer source: be018da0675741cf2c6aceaa0d56feff17f7b292.
- Original keycode ordering, Vial UID, matrix, layer count and macro count kept.
- Layout options remain their existing one-byte allocation; no storage added.
- Right-hand USB main configuration retained; flash matching firmware to both
  halves rather than mixing new core firmware with the old split protocol.
- PMW3360 chosen based on the working backup's sensor-register/CPI code, not
  the manufacturer's currently published PMW3389 choice. Physical sensor and
  all PCB pin assignments have not been independently verified.
- Boot2 matches both original backups byte-for-byte. This helps establish
  flash boot compatibility but does not guarantee recovery without BOOTSEL.
- Existing VIAL_INSECURE=yes retained (always unlocked, like published source).
- New Vial builds have a new BUILD_ID and can reset EEPROM configuration.
  Retain the original .vil export and record DPI, auto-layer and lighting
  settings separately. A full original flash restore is the rollback path.

## Backup Address-Range Check

The original UF2 backups in Documents each contain 1 MiB of flash payload
(2 MiB files because UF2 has block overhead). Stock and baseline code address
EEPROM at 0x101fe000, outside that captured range. Settings-like differences
between the two images appear at 0x100fe000. This strongly suggests 1 MiB flash
address wrapping, but a complete rollback should not rely on that assumption.

`picotool save -a` estimates flash capacity by checking repeated boot pages; it
does not independently read a JEDEC capacity. The completed checks captured and
verified `[0x10000000, 0x10100000)` and `[0x10100000, 0x10200000)` separately on
each half in BOOTSEL, then compared the lower and upper files. Identical ranges
within each half support wrapping; they do not independently identify the flash
chip. For future updates, preserve fresh verified per-half backups under new
filenames in Documents. Different range contents would require preserving and
reviewing the upper range before flashing. Do not overwrite original backups.
Binary files require the correct offset on restore.

The current firmware retains the existing EEPROM base and boot2 rather than
guessing a new flash mapping. Its flash payload must end below 0x100fe000 so neither
possible EEPROM region is included in the firmware programming operation.
Booting new firmware can still change settings even though the UF2 avoids them.

## Testing

See `tests/README.md` for host tests of extracted production C functions with
QMK interfaces stubbed. These cover configuration validation, startup ordering,
layer/default state, manual scroll interactions, accumulator resets and CPI
priority. They do not replace testing on the actual hardware.

Run the build as described in `BUILD.md`, using a new artifact label. Keep the
UF2, ELF, build log, source diff and version record together. The artifact is
not a replacement for your original full-flash backups.

## Flashing Gate And Rollback

For future updates, after backup checks and review, put each half into BOOTSEL
separately, with the inter-half cable disconnected. Programming is a separate, deliberate
step; the build script never flashes. Keep a known Bootloader key available in
the new keymap: the compiled defaults do NOT include one, and a new BUILD_ID
can erase the previous assignment. After each half boots successfully, connect
to it in Vial, identify a local key using Matrix Tester, assign Quantum >
Bootloader, and physically press it to verify re-entry before proceeding.
Each standalone controller identifies as the right side with MASTER_RIGHT.
The GUI reboot command is not a substitute: the pinned core excludes that
command in VIAL_INSECURE builds. Default Bootmagic is not configured for the
standalone right-side matrix either. No software method guarantees recovery if
the new image fails to run; physical BOOTSEL access might still require opening
the case.

Once both halves run the matching build, reconnect them while unpowered, attach
the Mac to the right half, import `layouts/current.vil`, confirm the scroll
layer, and test typing, modifiers, all layers, both encoders, OLEDs, lighting,
normal cursor movement, scroll direction/DPI, manual Drg/DrgT, and power-cycle
persistence. Export a new .vil after setting the dropdown.

For rollback, if BOOTSEL remains accessible, deliberately restore and verify the
matching original half's UF2 before rebooting. Do not cross the
left and right backup files. If the address-range check identifies independent
upper-flash contents, the rollback plan must include that verified range too.

## Historical Build

The earlier scroll-only build succeeded with the pinned toolchain. It is not the
current release; its UF2, ELF and build records are in the private archive:

`~/Documents/trackball59-recovery/artifacts/scroll-layer-pmw3360/trackballseries_59ctc_v2_splinky_vial.uf2`

SHA-256:
`0f5f7a2ec59e992924bc1fc8fa0cc762090857b440868413fdd7a8b1f5f50419`

Ten host C test groups passed at both -O0 and -O2 with AddressSanitizer and
UndefinedBehaviorSanitizer; two definition/compatibility tests also passed.
These automated checks did not exercise a real Vial import or hardware.
Subsequent on-device use was confirmed working by the user; the final combined
firmware and expanded test record are documented in `LANGUAGE-SHIFT.md`.
