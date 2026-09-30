# Language Tap / Shift Hold

Built, tested, and flashed to both halves with successful device verification
and USB enumeration. The user confirmed all behavior working, including language
taps and both Shifts. The known-good firmware is `firmware/language-shift.uf2`,
with checksum in `firmware/SHA256SUMS`; the matching layout is `layouts/current.vil`.

Historical build records and recovery snapshots are private, outside Git, under
`~/Documents/trackball59-recovery/artifacts/`. That archive contains the previous
working `scroll-layer-pmw3360/` build and fresh verified per-half snapshots in
`right-before-language-shift/` and `left-before-language-shift/`. These are not
repository-local artifacts or part of a GitHub clone. Keep the original full-flash
backups and layout exports in Documents; published copies do not replace them.

## Behavior

Reserve these two complete Mod-Tap keycodes:

| Vial assignment | Tap | Hold |
| --- | --- | --- |
| LSFT_T(KC_F23) | Vial macro M0 | Left Shift |
| RSFT_T(KC_F24) | Vial macro M1 | Right Shift |

An isolated tap released before 200 ms plays the macro once. Another key pressed
while pending immediately selects Shift, before that key is processed. Holding
past 200 ms selects Shift. A hold never emits a language tap on release.
Quick-tap repeat and retro tapping are disabled only for these two codes; they
also bypass same-hand chordal-tap decisions. Bare F23/F24 are unaffected and
the reserved Mod-Tap tap placeholders are consumed, not sent to the host.

Other Mod-Taps keep Vial's global settings, including the working home-row
modifiers. Tap Dance timing/behavior, scroll dropdown and direction are unchanged.
Language/Shift presses end Caps Word, matching the old tap dances rather than
turning into Caps Word inversion gestures.

M0/M1 remain editable in Vial. This implementation supports the existing
balanced basic-key shortcut sequences (including their 50 ms delays), not
recursive macros or advanced QMK actions. They run synchronously inside tap
processing; do not put layer/mod-tap/tap-dance/macro actions inside M0/M1.
Real held modifiers are restored after playback, so explicit macro key-up
events do not leave a physically held Ctrl/Shift/Command cleared. The macro
still emits its original intermediate release reports and 50 ms delay.

## Prepared Layout

Source: `layouts/history/scroll-layer-working.vil`, copied from
`~/Documents/trackball59_layer_scroll_updated.vil`.

Source SHA-256:
77c6f5bff8ce054cb2cb5c10408992def750ced446dc4299697338dacd3dcfe1

Current layout: `layouts/current.vil`, copied from
`~/Documents/trackball59_language_shift.vil`. Original Documents files are retained.

Only base-layer matrix (3,0) TD(0) and (8,0) TD(1) were replaced with the two
Mod-Taps above. Scroll layer remains 1. Macros, home-row mods, all other keys,
other tap dances, encoders and runtime settings are unchanged. TD0/1 definitions
remain available, but those two physical keys no longer use them.

Generated and checked by tools/migrate_language_shifts.py, which refuses to
overwrite existing outputs and verifies the source was not changed. Import
only AFTER installing this firmware: older firmware would emit F23/F24 taps.
If the source export is no longer current, export again before flashing and
regenerate from that export with a new destination filename.

## Build And Verification

See BUILD.md. The pinned core needs patches/qmk-settings-weak-hooks.patch:
five Vial settings functions become weak so the keyboard can override them.
Default bodies remain unchanged; no global settings or storage format changes.
The build script verifies/applies this exact patch without discarding other work
and archives all keyboard source files, the patch, tool versions and hashes.

Final known-good firmware: `firmware/language-shift.uf2`, copied without rebuilding
from the archived final build:
`~/Documents/trackball59-recovery/artifacts/language-shift-final/trackballseries_59ctc_v2_splinky_vial.uf2`.
The matching ELF, source snapshot, logs and toolchain records stay in that private
archive. Future local builds use ignored `artifacts/<label>/` directories.

SHA-256:
f9d46646e1a9bb4310a6eafe33e687d9bc8bd2c56dc27bb7468cd351e2064ad4

Tests: 28 native QMK cases pass, including real tapping/action processing,
both shifts, quick capitals, repeated taps, timeout holds, Caps Word, held
modifier restoration and home-row behavior. Balanced basic macro reports are
simulated using actual register/unregister functions, not the Vial macro parser.
11 host C groups pass at -O0 and -O2 with ASan/UBSan, plus two compatibility
checks. These suites do not perform a real Vial layout import or validate physical
hardware. The user separately confirmed the flashed firmware working on-device.

## Future Updates

First preserve the currently working state: export current layout and capture
a fresh verified UF2 from each half in BOOTSEL with new filenames. Retain the
original factory backups and the previous scroll-layer candidate unchanged.

Install any future reviewed build on both halves separately using the established
procedure. The new BUILD_ID may reset Vial data, so do not depend on the existing
Bootloader assignment surviving. Assign and test a Bootloader key through Vial
after each reboot. Reconnect both halves unpowered, then attach USB to the right.
Refresh Vial and import the matching language-shift layout. Building never flashes;
programming remains a separate, deliberate action.

Test short taps, fast Shift+letter with same/opposite hands, both shifts,
tap followed quickly by Shift+letter, long hold/release without language changes,
home-row Ctrl/Command shortcuts, and scroll-layer persistence. Host-side Kanata
should not also apply dual-role Shift behavior to this keyboard's output.

Rollback to the known-working scroll firmware using the fresh per-half UF2s
when available, or its preserved compiled candidate plus the pre-change .vil
export and separately recorded lighting/DPI settings. The latter does not
restore arbitrary persistent settings exactly. Physical bootloader access may
still require disassembly if firmware ever fails to run.
