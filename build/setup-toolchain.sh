#!/usr/bin/env bash
set -euo pipefail

usage() {
    printf '%s\n' 'Aufruf: bash build/setup-toolchain.sh [CACHE]' >&2
    exit 2
}
[[ $# -le 1 ]] || usage
for command in clang-19 ld.lld-19 curl sha256sum tar; do
    command -v "$command" >/dev/null || { printf 'Fehlendes Werkzeug: %s\n' "$command" >&2; exit 1; }
done

root=${1:-"$PWD/.cache/akeno-ps5"}
url='https://github.com/ps5-payload-dev/pacbrew-repo/releases/download/v0.40.2/ps5-payload-dev.tar.gz'
expected='a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d'
archive="$root/pacbrew-v0.40.2.tar.gz"
target='opt/ps5-payload-sdk/target'
mkdir -p "$root"
if [[ ! -f "$archive" ]]; then
    stage=$(mktemp "$root/pacbrew.XXXXXX")
    trap 'unlink "$stage" 2>/dev/null || true' EXIT
    curl --fail --show-error --silent --location --proto '=https' \
        --proto-redir '=https' --connect-timeout 15 --max-time 240 "$url" -o "$stage"
    printf '%s  %s\n' "$expected" "$stage" | sha256sum --check
    mv "$stage" "$archive"
fi
printf '%s  %s\n' "$expected" "$archive" | sha256sum --check
tar -xzf "$archive" -C "$root" --no-same-owner --no-same-permissions --wildcards \
    "$target/include/*" "$target/user/homebrew/include/SDL2/*" \
    "$target/user/homebrew/lib/libSDL2.a" "$target/user/homebrew/lib/libiconv.a" \
    "$target/user/homebrew/lib/libsamplerate.a" "$target/lib/libc.a" \
    "$target/lib/libSceSystemService.so" "$target/lib/libSceUserService.so" \
    "$target/lib/libScePad.so" "$target/lib/libSceAudioOut.so" "$target/lib/libSceVideoOut.so" \
    "$target/lib/libSceKeyboard.so" "$target/lib/libSceImeDialog.so" \
    "$target/lib/libSceLibcInternal.so" "$target/lib/libkernel.so"
printf 'PS5_SYSROOT=%s/%s\n' "$(cd "$root" && pwd)" "$target"
