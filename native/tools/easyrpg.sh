#!/usr/bin/env bash
# Akeno Gaming Hub - the integrated RPG Maker 2000/2003 runtime (EasyRPG Player, GPL-3.0).
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   native/tools/easyrpg.sh fetch           pinned sources into native/build/easyrpg/src
#   native/tools/easyrpg.sh host            static archive for the Linux host (system SDL2, pixman, png, zlib)
#   native/tools/easyrpg.sh ps5 <cc> <c++>  static archive for the PS5 title (everything built from source)
#
# EasyRPG Player is linked into Akeno's own eboot and runs in Akeno's play mode
# (src/play/play.cpp): the hub restarts itself with --play=<game> and the player takes
# the screen, the DualSense and the audio output. Nothing else is launched.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
native=$(cd "$here/.." && pwd)
root="$native/build/easyrpg"
src="$root/src"
jobs=${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}

# name url commit (annotated tags resolve to these commits)
pins=(
    "player https://github.com/EasyRPG/Player 78328fa29f465315291e161130e6682f69410370 0.8.1.1"
    "liblcf https://github.com/EasyRPG/liblcf 92c4450a1bc1acb58bd02bbb99b57e5036919cdf 0.8.1"
    "inih https://github.com/benhoyt/inih 5cc5e2c24642513aaa5b19126aad42d0e4e0923e r58"
    "fmt https://github.com/fmtlib/fmt e69e5f977d458f2650bb346dadf2ad30c5320281 10.2.1"
    "libpng https://github.com/pnggroup/libpng ea127968204cc5d10f3fc9250c306b9e8cbd9b80 v1.6.48"
)
pixman_url=http://archive.ubuntu.com/ubuntu/pool/main/p/pixman/pixman_0.42.2.orig.tar.gz
pixman_sha=ea1480efada2fd948bc75366f7c349e1c96d3297d09a3fe62626e38e234a625e

fetch() {
    mkdir -p "$src"
    for pin in "${pins[@]}"; do
        read -r name url commit tag <<<"$pin"
        if [[ -d $src/$name/.git ]] && [[ $(git -C "$src/$name" rev-parse HEAD) == "$commit" ]]; then
            continue
        fi
        rm -rf "$src/$name"
        GIT_LFS_SKIP_SMUDGE=1 git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "$url" "$src/$name"
        got=$(git -C "$src/$name" rev-parse HEAD)
        [[ $got == "$commit" ]] || { echo "$name: expected $commit, got $got" >&2; exit 1; }
        echo "  $name $tag ($commit)"
    done
    if [[ ! -f $src/pixman/pixman/pixman.c ]]; then
        local archive="$root/pixman-0.42.2.tar.gz"
        [[ -f $archive ]] || curl -fsSL -o "$archive" "$pixman_url"
        echo "$pixman_sha  $archive" | sha256sum -c --quiet - || { rm -f "$archive"; echo "pixman: hash mismatch" >&2; exit 1; }
        rm -rf "$src/pixman" && mkdir -p "$src/pixman"
        tar -xzf "$archive" -C "$src/pixman" --strip-components 1
        echo "  pixman 0.42.2 ($pixman_sha)"
    fi
}

# Generated headers shared by both builds.
configure() {
    local out=$1
    mkdir -p "$out/include/lcf" "$out/include/pixman"
    printf '#define LCF_SUPPORT_ICU 0\n#define LCF_SUPPORT_XML 0\n#define LCF_SUPPORT_INI 1\n' >"$out/include/lcf/config.h"
    sed -e 's/@PIXMAN_VERSION_MAJOR@/0/' -e 's/@PIXMAN_VERSION_MINOR@/42/' -e 's/@PIXMAN_VERSION_MICRO@/2/' \
        "$src/pixman/pixman/pixman-version.h.in" >"$out/include/pixman/pixman-version.h"
    cat >"$out/include/pixman/config.h" <<'EOF'
/* pixman for Akeno: generic C paths plus SSE2/SSSE3 (every PS5 and x86-64 host has them). */
#define PACKAGE "pixman"
#define HAVE_PTHREADS 1
#define TLS __thread
#define USE_SSE2 1
#define USE_SSSE3 1
#define HAVE_BUILTIN_CLZ 1
#define SIZEOF_LONG 8
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_GETPAGESIZE 1
EOF
    cp "$src/libpng/scripts/pnglibconf.h.prebuilt" "$out/include/pnglibconf.h"
}

# compile <compiler> <flags...> -- <sources...> into $objdir, listing objects in $objlist
compile_all() {
    local cc=$1; shift
    local flags=()
    while [[ $1 != -- ]]; do flags+=("$1"); shift; done
    shift
    local pids=() source object
    for source in "$@"; do
        object="$objdir/$(echo "${source#"$src"/}" | tr '/' '_').o"
        echo "$object" >>"$objlist"
        if [[ ! -f $object || $source -nt $object ]]; then
            "$cc" "${flags[@]}" -c "$source" -o "$object" &
            pids+=($!)
            if ((${#pids[@]} >= jobs)); then
                wait "${pids[0]}" || exit 1
                pids=("${pids[@]:1}")
            fi
        fi
    done
    for pid in "${pids[@]}"; do wait "$pid" || exit 1; done
}

player_sources() {
    # The Player's portable sources (its CMake list) plus the SDL2 screen and audio backends.
    grep -oE "src/[A-Za-z0-9_/]+\.cpp" "$src/player/CMakeLists.txt" | sort -u |
        grep -v -e '^src/platform/' | sed "s|^|$src/player/|"
    echo "$src/player/src/platform/sdl/sdl2_ui.cpp"
    echo "$src/player/src/platform/sdl/sdl2_audio.cpp"
    echo "$native/src/play/easyrpg/akeno_easyrpg.cpp" # Akeno's entry into the player
}

build() {
    local target=$1 cc=$2 cxx=$3
    shift 3
    local out="$root/$target"
    objdir="$out/obj"
    objlist="$out/objects.txt"
    mkdir -p "$objdir"
    : >"$objlist"
    configure "$out"
    local common=(-O2 -ffunction-sections -fdata-sections -w "$@")
    local pixman_c=() name
    for name in access access-accessors bits-image combine32 combine-float conical-gradient edge edge-accessors \
        fast-path filter general glyph gradient-walker image implementation linear-gradient matrix noop \
        radial-gradient region16 region32 solid-fill timer trap utils x86 sse2 ssse3 arm ppc mips; do
        pixman_c+=("$src/pixman/pixman/pixman-$name.c")
    done
    pixman_c+=("$src/pixman/pixman/pixman.c")
    local libs_c=("$src/inih/ini.c")
    if [[ $target == ps5 ]]; then
        compile_all "$cc" "${common[@]}" -DHAVE_CONFIG_H -I"$out/include/pixman" -I"$src/pixman/pixman" -msse2 -mssse3 \
            -- "${pixman_c[@]}"
        local png=(png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite
            pngwtran pngwutil)
        for name in "${png[@]}"; do libs_c+=("$src/libpng/$name.c"); done
        # zlib for libpng and the player (Akeno itself uses miniz, whose symbols are all mz_*).
        for name in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees \
            uncompr zutil; do libs_c+=("$ZLIB_DIR/$name.c"); done
        compile_all "$cc" "${common[@]}" -DPNG_ARM_NEON_OPT=0 -DPNG_INTEL_SSE_OPT=0 -I"$out/include" -I"$src/libpng" \
            -I"$ZLIB_DIR" -I"$src/inih" -- "${libs_c[@]}"
    else
        compile_all "$cc" "${common[@]}" -I"$src/inih" -- "${libs_c[@]}"
    fi
    compile_all "$cxx" "${common[@]}" -std=c++17 -fexceptions -frtti -I"$out/include" -I"$src/liblcf/src" \
        -I"$src/liblcf/src/generated" -I"$src/inih" -- "$src"/liblcf/src/*.cpp "$src"/liblcf/src/generated/*.cpp
    mapfile -t player < <(player_sources)
    compile_all "$cxx" "${common[@]}" -std=c++17 -fexceptions -frtti -DUSE_SDL=2 \
        -DSUPPORT_AUDIO=1 -DWANT_FMMIDI=1 -DWANT_DRWAV=1 -DFMT_HEADER_ONLY -DEP_VERSION='"0.8.1.1"' \
        -DEP_VERSION_MAJOR=0 -DEP_VERSION_MINOR=8 -DEP_VERSION_PATCH=1 -DEP_VERSION_TWEAK=1 \
        -DEP_VERSION_APPEND='"(Akeno)"' -DEP_VERSION_GIT='""' \
        -I"$src/player/src" -I"$out/include" -I"$out/include/pixman" -I"$src/pixman/pixman" -I"$src/liblcf/src" \
        -I"$src/liblcf/src/generated" -I"$src/fmt/include" -I"$src/libpng" ${ZLIB_DIR:+-I"$ZLIB_DIR"} "${SDL_CFLAGS[@]}" \
        -- "${player[@]}"
    rm -f "$out/libakeno-easyrpg.a"
    xargs ar rcs "$out/libakeno-easyrpg.a" <"$objlist"
    echo "$out/libakeno-easyrpg.a: $(wc -l <"$objlist") objects"
}

case ${1:-} in
fetch) fetch ;;
host)
    fetch
    read -r -a SDL_CFLAGS <<<"$(sdl2-config --cflags) $(pkg-config --cflags pixman-1 libpng)"
    build host "${CC:-cc}" "${CXX:-c++}"
    ;;
ps5)
    fetch
    [[ $# -ge 4 ]] || { echo "usage: $0 ps5 <cc> <c++> <sdl include dir> [zlib dir]" >&2; exit 2; }
    SDL_CFLAGS=(-I"$4" -D_THREAD_SAFE)
    ZLIB_DIR=${5:?zlib source directory}
    build ps5 "$2" "$3" -fPIC
    ;;
*)
    echo "usage: $0 fetch|host|ps5 ..." >&2
    exit 2
    ;;
esac
