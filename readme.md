# Trackball59 Custom Firmware

Personal Vial firmware and layout for an MCK Trackball59 with USB-C between
halves, RP2040 controllers, PMW3360-compatible sensor behavior, and right-side
host USB. Both halves were flashed and the owner confirmed scrolling and
language/Shift behavior working. This is not a universal build for every
Trackball59 hardware revision.

## Features

- **Configurable scroll layer:** Vial Layouts > Drag-scroll layer selects Off
  or layers 1-9. Holding MO(n) activates the chosen layer and trackball scrolling.
  The choice persists and is included in Vial layout exports.
- **Manual scrolling preserved:** Drg/DrgT and the selected layer independently
  request scrolling. Existing scroll direction and sniping behavior are retained.
- **Responsive language/Shift keys:** isolated taps run M0/M1; another key press
  immediately resolves the held key as Shift. An isolated hold resolves at
  200 ms. Home-row Mod-Tap settings and other tap dances remain unchanged.
- **Standard Vial UI:** no custom Vial application is required. A small pinned
  Vial-QMK patch permits per-key settings hooks without changing their defaults.

## Contents

| Path | Purpose |
| --- | --- |
| `trackballseries/` | Manufacturer source overlay with custom changes |
| `firmware/language-shift.uf2` | Exact tested program image for both halves |
| `firmware/SHA256SUMS` | Checksum of that image |
| `layouts/current.vil` | Working language-Shift layout with scroll layer 1 |
| `layouts/history/` | Original and intermediate layouts, for reference |
| `shell.nix`, `build.sh`, `patches/` | Pinned build environment and core patch |
| `tests/` | Host and native QMK regression tests |
| `tools/` | Explicit file-only layout migration and firmware inspection tools |

The compiled UF2 is a program image, **not a full-device backup**. Full-flash
UF2s, raw BIN reads, ELF files and old build logs are deliberately excluded.
The owner's private recovery archive is in `~/Documents/trackball59-recovery/`;
original factory backups and original layout exports also remain in Documents.
Keep recovery backups outside this repository.

## Setup And Build

See [BUILD.md](BUILD.md) for pinned revisions, Nix setup, upstream preparation,
and rebuilding. The build script never flashes. Run commands from this repo:

```sh
export QMK_DIR="$HOME/dev/vial-qmk" # prepare this checkout as BUILD.md describes
nix-shell shell.nix --run 'bash build.sh current'
```

See [tests/README.md](tests/README.md) to run regression tests. Tests use the
manufacturer baseline commit from this repository's retained Git history, so
use a full clone rather than a shallow or source-only download.

The published firmware is the exact known-good image, not automatically replaced
by a new build. Vial generates a random BUILD_ID, so repeated builds need not be
byte-identical and flashing a new build can reset settings.

## Using The Layout

Only import `layouts/current.vil` after installing compatible custom firmware.
Its two Shift assignments are `LSFT_T(KC_F23)` and `RSFT_T(KC_F24)`; this firmware
replaces those taps with macros M0 and M1. Stock firmware would send F23/F24.
M0/M1 are balanced macOS language-selection shortcut chords. Host shortcuts must
be configured accordingly; this repo does not install host configuration.

Refresh Vial Web, connect to the keyboard, and load the layout. Check the
Drag-scroll layer dropdown afterward. The original historical export contains
layout_options=-1 and cannot be imported directly into the dropdown firmware;
the compatible historical copy and current layout fix that value.

Read [CUSTOM-FIRMWARE.md](CUSTOM-FIRMWARE.md) for scrolling and recovery details,
and [LANGUAGE-SHIFT.md](LANGUAGE-SHIFT.md) for Shift semantics, macro limitations,
testing, and update precautions. Maintain a reachable Bootloader key. Each half
is updated separately; connect the halves while unpowered, then host USB to the
right half. Recovery from non-running firmware may require opening the case.

## Provenance And Publication

Based on [Aldexuan/firmware](https://github.com/Aldexuan/firmware), baseline
`be018da0675741cf2c6aceaa0d56feff17f7b292`, derived from Charybdis/QMK.
The original manufacturer README is preserved in [UPSTREAM.md](UPSTREAM.md).
Original author and license headers remain intact. Local additions are provided
under GPL-2.0-or-later; see [LICENSE](LICENSE). Upstream dependency components
retain their own licenses; their exact source revisions are in BUILD.md.

Git history is preserved to retain attribution and support baseline tests. The
manufacturer remote is named `upstream`. There is no personal GitHub `origin`
until one is created explicitly. No automatic commits or pushes are performed.

Before publishing, review `layouts/` for personal mappings and any newly added
macro text. The supplied M0/M1 contain shortcut key events, not passwords or
text snippets. Never add full-flash backups, which can contain older settings or
macro data. The inherited `.idea` files are optional manufacturer IDE metadata.
