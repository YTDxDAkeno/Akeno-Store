#!/usr/bin/env bash
# Akeno Gaming Hub - builds what tools/ps5-link adds to every link: the CRT (the boilerplate's
# app_crt.cpp), libps5runtime.a from runtime/, and the system module stubs a title may import.
# Adapted from lowbit/ps5-openrct2 tools/build-runtime.sh (GPL-3.0). The stubs leave out the
# modules a game process does not load (anything bound to them would be null at run time, so
# such an import must fail to link instead).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
native="$root/.deps/native-app"
sdk="$native/.deps/native/ps5-payload-sdk"
prefix="$root/.deps/ps5"
build="$root/build/runtime"
mkdir -p "$prefix/lib" "$prefix/stubs" "$build"

for stub in "$sdk"/target/lib/*.so; do
    case ${stub##*/} in
    libScePosixForWebKit.so | libkernel_sys.so | libkernel_web.so | libmonosgen-2.0.so) ;;
    *) cp "$stub" "$prefix/stubs/" ;;
    esac
done

for list in "$root"/runtime/stubs/*.txt; do
    module=$(basename "$list" .txt)
    sed 's/.*/void &(void) {}/' "$list" >"$build/$module.c"
    "$root/tools/ps5-cc" -c "$build/$module.c" -o "$build/$module.o"
    "$sdk/bin/prospero-lld" --shared -soname "$module.prx" -o "$prefix/stubs/$module.so" "$build/$module.o"
done

install_if_changed() {
    if cmp -s "$1" "$2"; then rm -f "$1"; else mv "$1" "$2"; fi
}

"$root/tools/ps5-c++" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -c "$native/tooling/native/app_crt.cpp" -o "$build/ps5-crt.o"
install_if_changed "$build/ps5-crt.o" "$prefix/lib/ps5-crt.o"

objects=()
for source in "$root"/runtime/*.c; do
    object="$build/$(basename "$source" .c).o"
    "$root/tools/ps5-cc" -std=gnu11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
        -c "$source" -o "$object"
    objects+=("$object")
done
rm -f "$build/libps5runtime.a"
llvm-ar-18 rcsD "$build/libps5runtime.a" "${objects[@]}"
install_if_changed "$build/libps5runtime.a" "$prefix/lib/libps5runtime.a"
echo "runtime: $prefix/lib/libps5runtime.a"
