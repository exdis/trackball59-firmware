# Trackball 59 Build

Compile only; these instructions do not flash firmware or access USB devices.
They prepare a local upstream checkout, including the patch and symlink described
below, without activating system or global QMK configuration. Run commands from
this repository (for example, `~/dev/trackball59-firmware`).

The known-good `firmware/language-shift.uf2` has been flashed to both halves;
the user confirmed all behavior working, including language taps and both Shifts.
Its checksum is recorded in `firmware/SHA256SUMS`. See `LANGUAGE-SHIFT.md`.

## Pins

| Input | Revision / version |
| --- | --- |
| Keyboard baseline (Aldexuan clone) | `be018da0675741cf2c6aceaa0d56feff17f7b292` |
| nixpkgs | `eaad089433ca2bb662274377d33df3d0e51ef28b` |
| nixpkgs unpacked SHA-256 | `sha256-H6GwaZzZf+4npqv0tph94w9tZddSjFjmQrVsW0z78uk=` |
| vial-kb/vial-qmk | `dd43959ae5c08d8a28d38a1acf7b04e86b14a344` |
| ChibiOS | `8bd61b804303f1614d574546c2dd735eeabb09f5` |
| ChibiOS-Contrib | `3ac181e4ca5cafddaf8b472baa1d09c2b24c77b6` |
| Pico SDK | `d0c5cac430cc955b65efa0e899748853d9a80928` |
| LUFA (shared USB headers required on RP2040) | `549b97320d515bfca2f95c145a67bd13be968faa` |
| printf | `c2e3b4e10d281e7f0f694d3ecbd9f320977288cc` |
| ARM GNU Toolchain | `15.3.Rel1`, GCC `15.3.1 20260627` |
| QMK CLI / Python / GNU Make | `1.2.0` / `3.14.7` / `4.4.1` |

Verified on `aarch64-darwin` with Nix `2.34.7`. `shell.nix` installs temporary
build dependencies, not a user profile. It retains the QMK CLI's Python
dependencies but excludes the package's unrelated AVR toolchain and flashers.

## Build

For a new upstream checkout, first verify the parent directory, then clone and
detach at the pinned revision. The example keeps both checkouts under `~/dev`:

```sh
export PATH=/nix/var/nix/profiles/default/bin:/usr/bin:/bin:/usr/sbin:/sbin
export QMK_DIR="$HOME/dev/vial-qmk"
ls -d "$HOME/dev"
git clone https://github.com/vial-kb/vial-qmk.git "$QMK_DIR"
git -C "$QMK_DIR" switch --detach dd43959ae5c08d8a28d38a1acf7b04e86b14a344
git -C "$QMK_DIR" submodule update --init --depth 1 \
  lib/chibios lib/chibios-contrib lib/pico-sdk lib/lufa lib/printf
nix-shell shell.nix --run 'bash build.sh current'
```

For an already prepared checkout, skip cloning and switching revisions. Do not
change the revision of a checkout another build is using:

```sh
export PATH=/nix/var/nix/profiles/default/bin:/usr/bin:/bin:/usr/sbin:/sbin
export QMK_DIR="$HOME/dev/vial-qmk"
git -C "$QMK_DIR" submodule update --init --depth 1 \
  lib/chibios lib/chibios-contrib lib/pico-sdk lib/lufa lib/printf
nix-shell shell.nix --run 'bash build.sh current'
```

`build.sh` checks upstream and submodule revisions and creates a symlink from
`$QMK_DIR/keyboards/trackballseries` to this repository's `trackballseries`.
It refuses to replace any existing directory or unrelated symlink. Backup
files are not modified. `TRACKBALL_SOURCE_DIR` can select a separately prepared
source snapshot, but an existing overlay must already point there.

Before using the overlay, the script applies
`patches/qmk-settings-weak-hooks.patch` to the pinned, tracked-clean upstream
core with `git apply --check` followed by `git apply`. An already patched
checkout is accepted via `git apply --reverse --check`, without reapplying or
reversing the patch. Staged upstream changes, partial patches, extra tracked
diffs, and a patch checksum mismatch are rejected; unrelated changes are never
reset or overwritten. The untracked overlay and generated build outputs remain
in place. Keep other untracked files out of the upstream core; they are not
part of the tracked-diff verification or upstream snapshot.

The patch only adds `__attribute__((weak))` to `qs_get_tapping_term`,
`get_hold_on_other_key_press`, `get_quick_tap_term`, `get_retro_tapping`, and
`get_chordal_hold` in `quantum/qmk_settings.c`. Bodies and QS-based defaults
are unchanged. This permits strong board implementations in `language_shift.c`
to specialize language Shift Mod-Taps while preserving QS-based fallback for
other keys. Board implementations are separate from this scaffolding.

The canonical full-index upstream diff must equal the saved patch, whose
SHA-256 is pinned in `build.sh`:
`a1c441fcb3822954954daa2fa836728b0e5131e9cf3439095c3bd96fc7de38ff`.
Any intentional patch revision must update the patch and its expected hash
together, against the same pinned upstream revision.

The exact make invocation is:

```sh
make -C "$QMK_DIR" -j4 trackballseries/59ctc/v2/splinky:vial \
  POINTING_DEVICE_DRIVER=pmw3360 \
  BUILD_DIR=".build/trackball59-current" \
  SKIP_VERSION=yes QMK_USERSPACE="$QMK_DIR"
```

The script exports `QMK_HOME=$QMK_DIR`, `SOURCE_DATE_EPOCH=0`, and `LC_ALL=C`.
`SKIP_VERSION=yes` suppresses date/git-dependent firmware metadata. `JOBS`
overrides the default of four workers. Use a new artifact label for each run;
existing artifact directories are never overwritten. Do not run simultaneous
builds in one upstream checkout or edit the overlay during compilation.

Successful runs save UF2, ELF, logs, versions, source hashes, the tracked source
diff, and copies of the build scaffolding in `artifacts/<label>/` (gitignored).
`source.tar` contains the entire selected overlay, including untracked/ignored
files such as new `language_shift.c/h` sources, with symlinks dereferenced.
`source.patch` remains a supplemental tracked diff, not the source snapshot.
Source hashes are checked across archiving and compilation. Do not edit inputs
during either step; a detected change rejects the run before publishing UF2/ELF.

The relocated repository has no historical `artifacts/` directory. Future builds
create it for ignored local outputs. Historical builds, inspection reports, and
per-half snapshots are privately archived at
`~/Documents/trackball59-recovery/artifacts/`, outside Git. Original recovery
backups and layout exports remain in Documents; a GitHub clone does not include
these private files. The published UF2 and layouts are selected copies, not a
replacement for full-flash recovery backups.

The copied `patches/qmk-settings-weak-hooks.patch` and actual `upstream.patch`
must match exactly. `inputs.sha256` records their SHA-256 hashes alongside
`source.tar`, `source.patch`, `shell.nix`, and `build.sh`; verify it from the
artifact directory with `sha256sum -c inputs.sha256`. The upstream diff and
patch file are checked again after compilation. The expected tracked upstream
modification is only the weak-hook patch.

To reproduce an archived overlay, first check an existing parent with `ls -d`,
create an empty snapshot directory there, and extract `source.tar` into it with
`tar -xf /path/to/artifacts/<label>/source.tar -C /path/to/snapshot`. Set
`TRACKBALL_SOURCE_DIR=/path/to/snapshot` and use a fresh pinned upstream
checkout, or an overlay already pointing at that snapshot. The script never
retargets an existing overlay.

## Static Inspection

`tools/inspect_firmware.py` requires explicit `--left` and `--right` original
backup paths, plus `--baseline` and `--elf` paths from the same compiled build.
It never assumes that backups live beside the repository. For a new local build:

```sh
python3 tools/inspect_firmware.py \
  --left "$HOME/Documents/trackball59-left-original.uf2" \
  --right "$HOME/Documents/trackball59-right-original.uf2" \
  --baseline artifacts/current/trackballseries_59ctc_v2_splinky_vial.uf2 \
  --elf artifacts/current/trackballseries_59ctc_v2_splinky_vial.elf \
  --upstream "$QMK_DIR"
```

Use your actual backup locations and build label. `--baseline` is the comparison
image, not necessarily the historical PMW3389 baseline. The published UF2 alone
does not provide the matching ELF: build locally or use the matching private
archive pair. Inspection is file-only, with no USB access. `--report` optionally
writes a new JSON file and refuses to overwrite an existing one. Optional
`--objdump` uses the system temporary directory for a disposable payload copy;
historical SROM downloads require `--historical-srom`.

## Baseline Record

The baseline below used PMW3389 as published. Subsequent inspection of the
original working firmware identified PMW3360 register/CPI behavior. The custom
build now selects PMW3360; this is a firmware match, not physical sensor-ID
verification. Do not flash the baseline artifact. See `CUSTOM-FIRMWARE.md` for
the completed backup checks and precautions for future updates.

The only keyboard-source change for the baseline was correcting
`trackballseries/59ctc/v2/splinky/config.h` from `keymaps/via/glcdfont.c` to
`keymaps/vial/glcdfont.c`. PMW3389 was already selected. Shared keyboard files
and the Vial keymap/definition were not edited.

The successful compile baseline is no longer a repository-local build output.
It is preserved only in the private archive:

```text
~/Documents/trackball59-recovery/artifacts/baseline-complete/trackballseries_59ctc_v2_splinky_vial.uf2
189440 bytes
SHA-256: 1821f5ff241c4a513ef20a78162e8c614e0f2f7b272bcabb218168d14dfd5605
```

`arm-none-eabi-size` reports `text=94604 data=0 bss=264864`. This is a compile
baseline, not hardware validation. The existing keymap sets `VIAL_INSECURE=yes`;
the compiler reports that mode and it has deliberately been preserved.

The `baseline-repeat` build also passed with matching source hashes, but a
different UF2 hash:
`908b6176b38b880d0854701a5c95086ac39aa3b53c8694a249ce0ac4f99f14e8`.
Upstream `util/build_id.py` inserts a random 24-bit `BUILD_ID` into generated
`version.h`, even with `SKIP_VERSION=yes`. These pins do not promise bit-identical
stock Vial output. The same caveat applies when rebuilding the current firmware.
