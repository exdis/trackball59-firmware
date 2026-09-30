# Focused Host Tests

Run from the repository root with Python 3, Git (including the pinned baseline
commit), and host Clang with AddressSanitizer/UBSan support:

```sh
python3 tests/run_host_tests.py --cc /usr/bin/clang
```

The runner checks that its build parent exists, creates a temporary directory
under `tests/`, and deletes generated files afterward. `--build-parent` can
select another existing temporary directory. `CC` or `--cc` selects a compiler.
No Python packages, QMK checkout, Nix shell, device access, or firmware build are
required. Only test files are added to the repository; no production or user
backup files are written. Git is used read-only.

## Actual Code Under Test

`run_host_tests.py` extracts the complete pointing-device section of the current
`trackballseries/trackballseries.c`, including its real configuration union,
static request flags/buffers, CPI logic, scroll task, initialization routines,
and **entire `process_record_kb` function**. Thus DRGSCRL/DRG_TOG tests execute the
production switch cases, not a test implementation of their behavior.

It also extracts the Vial keymap prefix containing the option/default-layer
hooks, readiness flag and post-init callback, plus the complete
`layer_state_set_user` and `process_record_user` functions. Function extraction
balances braces while ignoring comments and literals; section boundaries must
be unique. Missing/changed extraction boundaries fail the run. `#line` directives
retain production file/line diagnostics. The only removed include is `timer.h`;
none of the tested function bodies are rewritten. The real shared header and
Vial `config.h` are included, so keycode enum/aliases, ten-layer limit, and reverse-Y
setting are not duplicated in the harness.

`quantum.h` and the stubs in `host_dragscroll.c` supply only QMK interfaces:
master role, layer-bit comparison (including QMK's empty-state/base-layer
semantics), in-memory EEPROM, CPI call capture, modifiers, and a pass-through
pointing-device user callback. The real keymap `process_record_user` participates
in DRG key dispatch. Tests share the translation unit to inspect private state
and seed stale buffers for transition/reset assertions.

The language-shift hook in this scroll fixture is a controllable stub. A dispatch
gate test verifies that the extracted production `process_record_user` forwards
the original code/record on both edges, returns false when consumed, and skips
its own switch in that case. Actual language-shift behavior is tested separately
below, not modeled by this stub.

## Coverage

- All selector nibbles: Off, 1..9, invalid 10..15; ignored high bits; startup
  before readiness, persisted-option reload, and keyboard post-init sequencing.
- Live dropdown changes with already-active layers, selected layer below a
  higher layer, incoming state rather than stale globals, default-layer union,
  unchanged returned states, and master/nonmaster hooks.
- Actual DRG momentary press/release and toggle press/release while the selected
  layer remains active; manual mode outliving layer removal and vice versa.
- Both partial scroll buffers, exact threshold and signed/reversed scroll
  output, preservation while effective mode stays on, reset in both transition
  directions, no redundant CPI writes from idempotent setters, and initialization.
- CPI priority: drag-scroll over sniping over normal; DPI changes under an
  overriding mode; automatic layer sniping and restoration after scrolling.
- No EEPROM writes from selector/mode operations; expected initialization writes.
- JSON selector labels and order; all other JSON content equal to baseline
  `be018da0675741cf2c6aceaa0d56feff17f7b292`, including custom-keycode ordering,
  VID/PID, matrix and geometry. The existing layout has 61 matrix positions and
  four encoder actions, all checked separately, not 59 positions.
- UID `6da5cd8dc73d7ba8`, ten layers, 32 macros, keycode enum and matrix/config
  definitions remain baseline-compatible. Board config comparison allows only
  the existing OLED font-path correction.

## Limitations

These are focused host tests, not a full QMK build or an emulated keyboard. They
do not prove QMK's actual startup/callback order, VIA protocol dispatch or
EEPROM persistence, GUI dropdown packing, split RPC/synchronization, sensor CPI
effects, report rotation, USB HID behavior, or physical scrolling. The automatic
pointer-layer timer, OLED/RGB, full keymap array, and non-pointing-device build
are not compiled. Split config-sync is disabled. Host packed-bitfield ABI and
stub report types are not a substitute for target ABI validation. Only the
current reverse-Y configuration is exercised, not every feature-flag permutation.

The startup/master guards apply to the new drag-scroll hook: the existing
`layer_state_set_user` still invokes the sniping setter before readiness and on
nonmaster. Tests explicitly distinguish this existing behavior from the guarded
drag-scroll request; they do not claim all early/nonmaster CPI writes are blocked.

Compatibility checks establish unchanged source-level export addressing and
identity against the pinned baseline, **not** an actual Vial `.vil` import or
EEPROM migration. No user export is loaded or changed. Matching UID alone cannot
prove compatibility with arbitrary older firmware/backups. VIA's one-byte
layout-options default is an upstream assumption; the ten choices fit four bits,
but the upstream GUI/schema and generated EEPROM offsets are not tested here.

## Run Record

2026-09-30, Apple Clang 21.0.0, arm64 macOS:

```sh
python3 tests/run_host_tests.py --cc /usr/bin/clang --build-parent "${TMPDIR:-/tmp}"
```

Compiles at `-O0` and `-O2` with `-Wall -Wextra -Werror`, AddressSanitizer and
UBSan (`-fno-sanitize-recover=all`). Unused parameters/variables are exempted for
legacy debug/config code outside the focused coverage. Eleven C test groups pass
per optimization level; two Python compatibility checks pass. No sanitizer
diagnostics.

## Native QMK Language-Shift Tests

From the repository root on macOS (Apple Clang and the repository Nix shell),
prepare the pinned upstream checkout as described in `BUILD.md`. Initialize its
test dependency once, then run the suite:

```sh
export QMK_DIR="$HOME/dev/vial-qmk"
git -C "$QMK_DIR" submodule update --init --depth 1 lib/googletest
env PATH=/nix/var/nix/profiles/default/bin:/usr/bin:/bin:/usr/sbin:/sbin \
  nix-shell shell.nix --run 'python3 tests/run_language_shift_tests.py --qmk "$QMK_DIR" --build-parent "${TMPDIR:-/tmp}"'
```

Unlike the host runner, this runner requires both `--qmk` and `--build-parent`;
the latter must be an existing directory. Neither runner needs private recovery
archives, compiled firmware, or layout exports. The preserved Git baseline commit
is sufficient for the host compatibility checks.

The runner creates only an untracked `tests/trackball_language_shift` symlink in
upstream, pointing to this repository's `tests/qmk_language_shift`. It refuses
to replace an existing unrelated path. It uses `builddefs/build_test.mk` directly
and a unique temporary `BUILD_DIR` for **all** objects, including googletest, so
neither default outputs nor a concurrent firmware build are touched. Temporary
builds are removed afterward; the reusable test symlink remains. No tracked
upstream edits are needed. Production source and user backups are not changed.

The fixture compiles the current production `language_shift.c/h` with upstream
QMK headers, the real QS structure/bit definitions, and QMK's actual
`action_tapping.c`, `action.c`, keyboard scanning, and report generation. It uses
upstream googletest matrix/timer/host-driver mocks, **not a tap/hold model**.
Mac overrides select `/usr/bin/clang`, C++17 and libc++ instead of Linux linker
flags. Three upstream Clang diagnostics are disabled: strict prototypes,
include-next absolute paths, and C++11 narrowing in report matchers.

Coverage:

- Direct production-C policy checks over **all 65,536 keycodes**, all 16 QS
  tapping-bit combinations, same/opposite hands, and terms 0, 1, 321, 65535.
  Only the two complete Mod-Tap codes get 200 ms, hold-on-press, quick-tap 0,
  retro false, and chordal true. Every other code uses the exact QS fallback;
  the whole QS structure remains unchanged, including unrelated nonzero fields.
- Direct dispatch over all keycodes, tap counts 0/1/2/15 and both edges. Only
  language taps are consumed, macro 0/1 fires once on press, and holds pass.
- Real matrix sequences for both language shifts: 199 ms tap with no placeholder
  reports, 200 ms timeout independent of global 350 ms, rapid same-hand capital
  despite global chordal/retro, tap then quick second hold, and simultaneous
  left/right holds with independent releases.
- Home-row `LCTL_T(KC_S)` retains global tapping term, retro tapping, chordal
  same-hand veto, quick-tap repeat, and hold-on-other-press on/off behavior.
  Standalone F23/F24 still produce normal reports. Strict report expectations
  detect unwanted placeholder output in language-shift sequences.
- Real QMK Caps Word is enabled. Both language keys end it on resolved taps,
  timeout holds, and rapid interrupted holds. Direct production-hook tests
  isolate the press-only rule: reserved releases and unrelated keycodes do not
  turn it off through this hook.
- A selectable M0/M1 simulator calls real QMK `register_code`/`unregister_code`
  with basic keys: left Ctrl, Shift, Gui, optional Alt for M1, and Backslash;
  `wait_ms(50)` precedes reverse-order release. Tests compare every generated
  host report and the exact 50 ms Backslash-down interval on QMK's test clock.
  Both macros run with no held mods, left Ctrl, left Shift, left Ctrl+Shift,
  and right Ctrl+Shift pressed via the matrix. The original real mods and host
  report are restored, affect the next letter, and clear on physical release.
  Macro dispatch remains exactly once, with no F23/F24 or stuck-key reports.

Boundaries: macro dispatch defaults to a call-count/ID spy; dedicated tests
select the balanced basic-chord simulator. **Only M0/M1 balanced basic-key
chords are supported by this fixture and preservation contract**, not arbitrary
advanced macros, unbalanced key actions, layer/one-shot operations, or modifiers
intentionally left changed. This is not the Vial macro parser, execution of
EEPROM payloads, or OS language switching. Macro key-up reports can transiently
clear overlapping held modifiers before production restores them; tests verify
that exact sequence, not uninterrupted modifier-down state. QS persistence is
mocked; the fixture bridges
`get_tapping_term` to `qs_get_tapping_term` just like Vial's ordinary-key path and
bridges permissive hold to QS. It does not link the complete Vial raw-HID,
tap-dance or settings persistence stack. Synthetic per-test keymaps and hand
positions replace the device layout. The separate scroll fixture checks the
actual keymap dispatch gate. Physical USB, split transport, full firmware linking,
and target timing/ABI remain outside these tests.

Run record, 2026-09-30, rerun after Caps Word/modifier-preservation update:
**28/28 native QMK tests pass** on arm64 macOS with
Apple Clang 21.0.0, upstream `dd43959ae5c08d8a28d38a1acf7b04e86b14a344`, and
googletest `e2239ee6043f73722e7aa812a459f54a28552929`. The native suite is not
sanitizer-instrumented. The separate scroll/dispatch suite also passes all eleven
C groups at each of `-O0`/`-O2` with ASan/UBSan, plus both Python compatibility
checks. Both halves have since been flashed, and the user confirmed all behavior
working, including both Shifts. That is separate on-device validation, not a claim
that these automated suites execute real Vial imports or hardware tests.
