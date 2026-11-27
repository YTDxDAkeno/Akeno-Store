#!/usr/bin/env bash
# Akeno Gaming Hub - fetches the public PS5 build tools at pinned versions.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ps5-native-app-boilerplate (GPL-3.0-or-later) provides the public PS5 payload SDK v0.42,
# ps5-native-tool (PIE -> PS5 module conversion and FSELF signing) and the clean-room
# sce_module/libc.prx. Its hardware-tested libc.prx digest is verified after the build.
# No Sony SDK file is used.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
deps="$root/.deps"
native="$deps/native-app"
repo=https://github.com/drakmor/ps5-native-app-boilerplate.git
commit=470695e0c557f99ff2df0e36e4df713c5e636526

for command in git clang++-18 clang-18 ld.lld-18 llvm-ar-18 wget unzip tar sha256sum python3 make; do
    command -v "$command" >/dev/null || { echo "missing tool: $command (install clang-18 lld-18 llvm-18)" >&2; exit 2; }
done
mkdir -p "$deps"
if [[ ! -d $native/.git ]]; then
    git -c core.autocrlf=false clone -q "$repo" "$native"
fi
if [[ $(git -C "$native" rev-parse HEAD) != "$commit" ]]; then
    git -C "$native" fetch -q origin "$commit" 2>/dev/null || git -C "$native" fetch -q --unshallow 2>/dev/null || true
    git -C "$native" -c advice.detachedHead=false checkout -q "$commit"
fi

# zlib for the host tool: the boilerplate pins 1.3.2 from zlib.net; the same archive is
# published on GitHub and is accepted only with the pinned SHA-256.
zdir="$native/.deps/native/zlib"
zarchive="$zdir/zlib-1.3.2.tar.gz"
zhash=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
mkdir -p "$zdir"
if [[ ! -f $zarchive ]] || ! echo "$zhash  $zarchive" | sha256sum --check --quiet 2>/dev/null; then
    for url in https://zlib.net/fossils/zlib-1.3.2.tar.gz https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz; do
        if wget -q "$url" -O "$zarchive.part" && echo "$zhash  $zarchive.part" | sha256sum --check --quiet 2>/dev/null; then
            mv "$zarchive.part" "$zarchive"
            break
        fi
    done
    rm -f "$zarchive.part"
fi
bash "$native/tools/setup-native-dependencies.sh" >/dev/null

# The module converter at 470695e anchors the writable RELRO segment at the GOT's file offset.
# A C++ program with .data.rel.ro then gets a LOAD whose file offset and address disagree at
# the 16 KiB page size, and the console refuses to start it (CE-107750-0). The boilerplate
# fixed this later; ProsperoStore carries the fixed converter (GPL-3.0-or-later, commit
# 0e1f031). These four files replace the older ones, each checked by SHA-256.
fixed=0e1f031889643130af0a617b58b684e9cbfa67af
overlay() {
    local file=$1 hash=$2 target="$native/tooling/native/$1"
    if ! echo "$hash  $target" | sha256sum --check --quiet 2>/dev/null; then
        curl -fsSL -o "$target.part" "https://raw.githubusercontent.com/darkxex/ProsperoStore/$fixed/tooling/native/$file"
        echo "$hash  $target.part" | sha256sum --check --quiet
        mv "$target.part" "$target"
        rm -f "$native/build/host/ps5-native-tool"
    fi
}
overlay sce_module_writer.cpp 30dacae8f192f2e9a81f7091c37bd5443f22c595a17e5506a13f7bce3c8cea75
overlay sce_module_writer.hpp fbeec6a4a0e1e949342d943d0e0979a981a69dd33f88e86221f14ae9a0d19591
overlay native_app_builder.cpp 0de3002b5148598ca739f84a6935146593858bc66382fa4baebea691625bd71c
overlay self_container.cpp 3c028f6523b4c169e8b909cb2722187f13eea1487a5848c1d191693a854bb695

sdk="$native/.deps/native/ps5-payload-sdk"
zroot="$native/.deps/native/zlib/root"
tool="$native/build/host/ps5-native-tool"
if [[ ! -x $tool ]]; then
    mkdir -p "$(dirname "$tool")"
    clang++-18 -std=c++20 -O2 -Wall -Wextra -I "$zroot/usr/include" \
        "$native/tooling/native/native_app_builder.cpp" "$native/tooling/native/self_container.cpp" \
        "$native/tooling/native/elf_object.cpp" "$native/tooling/native/sce_module_writer.cpp" \
        "$(find "$zroot" -name libz.a -print -quit)" -o "$tool"
fi
if [[ ! -f $native/runtime/libc.prx ]]; then
    (cd "$native" && bash tools/rebuild-libc.sh >/dev/null)
fi
(cd "$native/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
echo "PS5 payload SDK: $sdk"
echo "ps5-native-tool: $tool (module converter from ProsperoStore $fixed)"
echo "libc.prx: $(sha256sum "$native/runtime/libc.prx" | cut -d' ' -f1) (matches the boilerplate's hardware-tested digest)"
