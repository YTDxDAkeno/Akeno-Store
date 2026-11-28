#!/usr/bin/env bash
# Akeno Gaming Hub - builds the PS5 title folder PPSA90410 and its install ZIP.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   bash ps5/build.sh            fetch the pinned tools, build, sign, package
#
# Output: ps5/dist/PPSA90410/ (eboot.bin, sce_module/libc.prx, sce_sys/, assets/) and
# ps5/dist/PPSA90410.zip with the title folder at the top, the layout ShadowMountPlus,
# ProsperoStore and Akeno itself install. Requires Linux or WSL with clang-18, lld-18,
# llvm-18, cmake, ninja, python3 (Pillow for the icon), git, curl, wget, unzip.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$root/.." && pwd)
src="$repo/native"
deps="$root/.deps"
prefix="$deps/ps5"
native="$deps/native-app"
sdk="$native/.deps/native/ps5-payload-sdk"
tool="$native/build/host/ps5-native-tool"
build="$root/build"
dist="$root/dist"
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/sce_sys/param.json")
version=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["contentVersion"])' "$root/sce_sys/param.json")
jobs=${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}

echo "==> [1/6] Toolchain (public PS5 payload SDK, ps5-native-tool, libc.prx)"
bash "$root/tools/fetch-toolchain.sh" | sed 's/^/    /'
bash "$root/tools/build-runtime.sh" | sed 's/^/    /'
echo "==> [2/6] SDL 2 for the title, integrated runtimes"
bash "$root/tools/build-sdl.sh" | sed 's/^/    /'
# RPG Maker 2000/2003: EasyRPG Player 0.8.1.1 built from pinned sources into Akeno's eboot.
zlib_dir="$root/.deps/native-app/.deps/native/zlib/zlib-1.3.2"
bash "$src/tools/easyrpg.sh" ps5 "$root/tools/ps5-cc" "$root/tools/ps5-c++" "$prefix/include/SDL2" "$zlib_dir" |
    sed 's/^/    /'
runtime_libs=("$src/build/easyrpg/ps5/libakeno-easyrpg.a")

echo "==> [3/6] Compiling Akeno for the PS5"
mkdir -p "$build/obj"
includes=(-I"$src/src" -I"$src/third_party/yyjson" -I"$src/third_party/monocypher" -I"$src/third_party/miniz"
    -I"$src/third_party/picosha2" -I"$src/third_party/stb" -I"$src/third_party/qrcodegen" -I"$prefix/include/SDL2")
cxx_sources=($(cd "$src" && ls src/core/*.cpp src/net/*.cpp src/stores/*.cpp src/install/*.cpp src/runtime/*.cpp \
    src/play/*.cpp src/ui/*.cpp src/main.cpp src/platform/ps5.cpp src/platform/ps5_http.cpp))
c_sources=(third_party/yyjson/yyjson.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c
    third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c
    third_party/qrcodegen/qrcodegen.c)
objects=()
compile() {
    local source=$1 object=$2
    if [[ $source == *.cpp ]]; then
        "$root/tools/ps5-c++" -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
            -Wall -Wno-missing-field-initializers -DAKENO_ASSET_DIR='"/app0/assets"' -DAKENO_WITH_EASYRPG=1 "${includes[@]}" \
            -c "$src/$source" -o "$object"
    else
        "$root/tools/ps5-cc" -std=gnu11 -O2 -ffunction-sections -fdata-sections -DMINIZ_NO_TIME \
            "${includes[@]}" -c "$src/$source" -o "$object"
    fi
}
pids=()
for source in "${cxx_sources[@]}" "${c_sources[@]}"; do
    object="$build/obj/${source//\//_}.o"
    objects+=("$object")
    if [[ ! -f $object || $src/$source -nt $object || -n $(find "$src/src" -name '*.hpp' -newer "$object" -print -quit) ]]; then
        compile "$source" "$object" &
        pids+=($!)
        if (( ${#pids[@]} >= jobs )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
    fi
done
for pid in "${pids[@]}"; do wait "$pid"; done

echo "==> [4/6] Linking and signing eboot.bin"
"$root/tools/ps5-link" "${objects[@]}" "${runtime_libs[@]}" "$prefix/lib/libSDL2.a" -o "$build/eboot.elf"
mkdir -p "$build/tile"
"$root/tools/ps5-cc" -std=gnu11 -O2 -c "$root/tile/main.c" -o "$build/tile/main.o"
"$root/tools/ps5-link" "$build/tile/main.o" -o "$build/tile/eboot.elf"
# Refuse modules the console could not map (this caught CE-107750-0 on the first build).
python3 "$root/tools/check-module.py" "$build/eboot.elf" "$build/tile/eboot.elf"
echo "==> [5/6] Launch payload"
PS5_PAYLOAD_SDK="$sdk" "$sdk/bin/prospero-clang" -O2 -Wall -o "$build/akeno-launch.elf" "$root/payload/launch.c" \
    -lSceSystemService -lSceUserService
grep -q "AKENO-LAUNCH-REQUESTER-SLOT-0001" "$build/akeno-launch.elf"

echo "==> [6/6] Title folder $title_id ($version)"
app="$dist/$title_id"
rm -rf "$app"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/assets/fonts" "$app/assets/payload" "$app/assets/tile" "$app/assets/catalog" "$app/licenses"
"$tool" self --sign --in "$build/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F >/dev/null
cp "$native/runtime/libc.prx" "$app/sce_module/libc.prx"
cp "$root/sce_sys/param.json" "$app/sce_sys/param.json"
[[ -f $root/sce_sys/icon0.png ]] || python3 "$root/tools/make-icon.py" "$root/sce_sys/icon0.png"
cp "$root/sce_sys/icon0.png" "$app/sce_sys/icon0.png"
cp "$src/assets/fonts/"*.ttf "$app/assets/fonts/"
cp "$src/assets/catalog/akeno.json" "$app/assets/catalog/akeno.json"
cp "$build/akeno-launch.elf" "$app/assets/payload/akeno-launch.elf"
"$tool" self --sign --in "$build/tile/eboot.elf" --out "$app/assets/tile/eboot.bin" --magic 0x1D3D154F >/dev/null
cp "$native/runtime/libc.prx" "$app/assets/tile/libc.prx"
cp "$repo/LICENSE" "$app/licenses/Akeno-GPL-3.0.txt"
cp "$src/assets/fonts/Inter-LICENSE.txt" "$app/licenses/Inter-OFL.txt"
cp "$repo/THIRD_PARTY_LICENSES.md" "$app/licenses/THIRD_PARTY_LICENSES.md"
# The integrated runtimes' licences travel with the binary that contains them.
easyrpg_src="$src/build/easyrpg/src"
cp "$easyrpg_src/player/COPYING" "$app/licenses/EasyRPG-Player-GPL-3.0.txt"
cp "$easyrpg_src/liblcf/COPYING" "$app/licenses/liblcf-MIT.txt"
cp "$easyrpg_src/pixman/COPYING" "$app/licenses/pixman-MIT.txt"
cp "$easyrpg_src/libpng/LICENSE" "$app/licenses/libpng.txt"
cp "$easyrpg_src/fmt/LICENSE" "$app/licenses/fmt-MIT.txt"
cp "$easyrpg_src/inih/LICENSE.txt" "$app/licenses/inih-BSD-3-Clause.txt"
cp "$zlib_dir/LICENSE" "$app/licenses/zlib.txt"
chmod -R 0777 "$app"
"$tool" self --inspect --file "$app/eboot.bin" | sed 's/^/    /'
"$tool" self --inspect --file "$app/sce_module/libc.prx" | grep -E "integrity" | sed 's/^/    libc.prx /'

# The ZIP holds the title folder at the top, with fixed timestamps for reproducible bytes.
python3 - "$dist" "$title_id" <<'PY'
import hashlib, os, sys, zipfile
dist, title = sys.argv[1], sys.argv[2]
root = os.path.join(dist, title)
out = os.path.join(dist, title + ".zip")
with zipfile.ZipFile(out + ".part", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for folder, dirs, files in os.walk(root):
        dirs.sort()
        # Directory entries carry 0777 too: the console starts only titles every user can read.
        entry = zipfile.ZipInfo(os.path.relpath(folder, dist) + "/", date_time=(2026, 11, 27, 0, 0, 0))
        entry.external_attr = (0o40777 << 16) | 0x10
        archive.writestr(entry, b"")
        for name in sorted(files):
            path = os.path.join(folder, name)
            info = zipfile.ZipInfo(os.path.relpath(path, dist), date_time=(2026, 11, 27, 0, 0, 0))
            info.external_attr = (0o100777 << 16)
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as source:
                archive.writestr(info, source.read())
os.replace(out + ".part", out)
digest = hashlib.sha256(open(out, "rb").read()).hexdigest()
with open(os.path.join(dist, "SHA256SUMS"), "w") as sums:
    sums.write(f"{digest}  {title}.zip\n")
    for folder, _, files in os.walk(root):
        for name in sorted(files):
            path = os.path.join(folder, name)
            sums.write(f"{hashlib.sha256(open(path,'rb').read()).hexdigest()}  {os.path.relpath(path, dist)}\n")
print(f"    {out}: {os.path.getsize(out)} bytes, sha256 {digest}")
PY
echo "Done: $app"
