#!/usr/bin/env bash
# Akeno Gaming Hub - builds SDL 2 for the PS5 title from ps5-payload-dev's fork (zlib license)
# at the commit OpenRCT2 for PS5 runs on hardware, with that port's patches (patches/sdl).
# Video is SDL's VideoOut framebuffer driver drawn by the software renderer; audio, ScePad and
# the IME dialog come from the same fork.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
deps="$root/.deps"
prefix="$deps/ps5"
build="$root/build/sdl"
commit=ee4c47dc0d617b3bc8f35108f9956baf228a1322
hash=869cf9d26ae7ff9f73c05762f0a5cc988655d90bd7ce9167c4d4e0d76634dd31
archive="$deps/downloads/SDL-$commit.tar.gz"
mkdir -p "$deps/downloads" "$deps/src"

if [[ -f $prefix/lib/libSDL2.a && -f $prefix/.sdl-$commit ]]; then
    echo "SDL2: $prefix/lib/libSDL2.a (cached)"
    exit 0
fi
source="$deps/src/SDL-$commit"
rm -rf "$source"
if [[ -f $archive ]] && echo "$hash  $archive" | sha256sum --check --quiet 2>/dev/null ||
    { curl -fsSL -o "$archive.part" "https://github.com/ps5-payload-dev/SDL/archive/$commit.tar.gz" 2>/dev/null &&
        echo "$hash  $archive.part" | sha256sum --check --quiet && mv "$archive.part" "$archive"; }; then
    tar -xf "$archive" -C "$deps/src" --exclude='*/android-project-ant'
else
    # Archive downloads may be blocked; git verifies the same tree through the commit hash.
    rm -f "$archive.part"
    git clone -q --filter=blob:none --no-checkout https://github.com/ps5-payload-dev/SDL.git "$source"
    git -C "$source" -c advice.detachedHead=false checkout -q "$commit"
    [[ $(git -C "$source" rev-parse HEAD) == "$commit" ]]
    rm -rf "$source/.git" "$source/android-project-ant"
fi
for patch in "$root"/patches/sdl/*.patch; do
    patch -d "$source" -p1 --quiet <"$patch"
done
rm -rf "$build"
cmake -S "$source" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$root/cmake/ps5.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_OPENGL=OFF -DSDL_LIBSAMPLERATE=OFF \
    -DSDL_LOADSO=OFF >"$build.log" 2>&1 || { tail -40 "$build.log"; exit 1; }
ninja -C "$build" install >>"$build.log" 2>&1 || { tail -40 "$build.log"; exit 1; }
touch "$prefix/.sdl-$commit"
echo "SDL2: $prefix/lib/libSDL2.a"
