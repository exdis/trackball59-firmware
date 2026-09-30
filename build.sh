#!/usr/bin/env bash
set -euo pipefail

repo=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
qmk_dir=${QMK_DIR:?Set QMK_DIR to the pinned vial-qmk checkout (see BUILD.md)}
qmk_dir=$(realpath "$qmk_dir")
source_dir=$(realpath "${TRACKBALL_SOURCE_DIR:-$repo/trackballseries}")
label=${1:-current}
case "$label" in
    ''|*[!a-zA-Z0-9_-]*) printf 'Invalid artifact label: %s\n' "$label" >&2; exit 1 ;;
esac

pin=dd43959ae5c08d8a28d38a1acf7b04e86b14a344
if [[ $(git -C "$qmk_dir" rev-parse HEAD) != "$pin" ]]; then
    printf 'Expected vial-qmk revision %s\n' "$pin" >&2
    exit 1
fi
for module in chibios chibios-contrib pico-sdk lufa printf; do
    expected=$(git -C "$qmk_dir" rev-parse "HEAD:lib/$module")
    actual=$(git -C "$qmk_dir/lib/$module" rev-parse HEAD)
    [[ "$expected" == "$actual" ]] || { printf 'Wrong submodule: %s\n' "$module" >&2; exit 1; }
done

patch="$repo/patches/qmk-settings-weak-hooks.patch"
patch_sha256=a1c441fcb3822954954daa2fa836728b0e5131e9cf3439095c3bd96fc7de38ff
[[ $(sha256sum "$patch" | cut -d ' ' -f 1) == "$patch_sha256" ]] || {
    printf 'Unexpected weak-hook patch checksum: %s\n' "$patch" >&2
    exit 1
}
upstream_diff() {
    git -C "$qmk_dir" diff --no-ext-diff --no-textconv --no-color --binary --full-index \
        --no-renames --src-prefix=a/ --dst-prefix=b/ --diff-algorithm=myers \
        --no-indent-heuristic --ignore-submodules=none -U3 HEAD
}
# Never reset unrelated work, including staged changes hidden by the worktree.
git -C "$qmk_dir" diff --cached --quiet || {
    printf 'Refusing staged upstream changes.\n' >&2
    exit 1
}
if git -C "$qmk_dir" diff --quiet --ignore-submodules=none HEAD; then
    git -C "$qmk_dir" apply --check "$patch"
    git -C "$qmk_dir" apply "$patch"
else
    git -C "$qmk_dir" apply --reverse --check "$patch"
fi
[[ $(upstream_diff | sha256sum | cut -d ' ' -f 1) == "$patch_sha256" ]] || {
    printf 'Upstream diff must match the weak-hook patch exactly; no changes were discarded.\n' >&2
    exit 1
}

overlay="$qmk_dir/keyboards/trackballseries"
if [[ -e "$overlay" || -L "$overlay" ]]; then
    [[ -L "$overlay" && $(realpath "$overlay") == "$source_dir" ]] || {
        printf 'Refusing to replace existing overlay: %s\n' "$overlay" >&2
        exit 1
    }
else
    ls -d "$qmk_dir/keyboards"
    ln -s "$source_dir" "$overlay"
fi

ls -d "$repo"
mkdir -p "$repo/artifacts"
out="$repo/artifacts/$label"
[[ ! -e "$out" ]] || { printf 'Artifact directory already exists: %s\n' "$out" >&2; exit 1; }
ls -d "$repo/artifacts"
mkdir "$out"

# Keep build metadata deterministic and ignore any globally configured QMK home.
export QMK_HOME="$qmk_dir"
export SOURCE_DATE_EPOCH=0
export LC_ALL=C
{
    printf 'vial-qmk: %s\nsource: %s\n' "$pin" "$source_dir"
    git -C "$repo" rev-parse HEAD
    git -C "$qmk_dir" submodule status
    arm-none-eabi-gcc --version
    qmk --version
    make --version
    python3 --version
} > "$out/versions.txt" 2>&1
git -C "$repo" diff --binary HEAD > "$out/source.patch"
cp "$repo/shell.nix" "$repo/build.sh" "$out/"
ls -d "$out"
mkdir "$out/patches"
cp "$patch" "$out/patches/"
upstream_diff > "$out/upstream.patch"
cmp "$out/patches/$(basename "$patch")" "$out/upstream.patch"
[[ $(sha256sum "$out/upstream.patch" | cut -d ' ' -f 1) == "$patch_sha256" ]]
find -L "$source_dir" -type f -exec sha256sum {} + | sort > "$out/source.sha256"
# Include untracked/ignored sources and dereference any source symlinks.
tar -chf "$out/source.tar" -C "$source_dir" .
find -L "$source_dir" -type f -exec sha256sum {} + | sort > "$out/source-archived.sha256"
cmp "$out/source.sha256" "$out/source-archived.sha256" || {
    printf 'Source changed during archiving; retry with a new label.\n' >&2
    exit 1
}
(
    cd "$out"
    sha256sum source.tar source.patch shell.nix build.sh \
        "patches/$(basename "$patch")" upstream.patch > inputs.sha256
)

make -C "$qmk_dir" -j"${JOBS:-4}" trackballseries/59ctc/v2/splinky:vial \
    POINTING_DEVICE_DRIVER=pmw3360 \
    BUILD_DIR=".build/trackball59-$label" \
    SKIP_VERSION=yes QMK_USERSPACE="$qmk_dir" \
    2>&1 | tee "$out/build.log"
firmware=trackballseries_59ctc_v2_splinky_vial
find -L "$source_dir" -type f -exec sha256sum {} + | sort > "$out/source-after.sha256"
cmp "$out/source.sha256" "$out/source-after.sha256" || {
    printf 'Source changed during compilation; retry with a new label.\n' >&2
    exit 1
}
upstream_diff > "$out/upstream-after.patch"
cmp "$out/upstream.patch" "$out/upstream-after.patch"
cmp "$patch" "$out/patches/$(basename "$patch")"
cp "$qmk_dir/.build/trackball59-$label/$firmware.uf2" "$out/$firmware.uf2"
cp "$qmk_dir/.build/trackball59-$label/$firmware.elf" "$out/$firmware.elf"
sha256sum "$out/$firmware.uf2" | tee "$out/firmware.sha256"
printf 'Build saved to %s\n' "$out"
