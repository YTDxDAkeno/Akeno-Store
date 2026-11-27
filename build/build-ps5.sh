#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mode=${1:-module}
case "$mode" in
    module)
        shift $(( $# > 0 ? 1 : 0 ))
        cache=${1:-"$PWD/.cache/akeno-ps5"}
        sysroot=${PS5_SYSROOT:-"$cache/opt/ps5-payload-sdk/target"}
        make ps5-module PS5_SYSROOT="$sysroot" PS5_CLANG="${PS5_CLANG:-clang-19}" PS5_LLD="${PS5_LLD:-ld.lld-19}"
        bash build/validate-ps5.sh module dist/ps5/akeno-ui.so
        sha256sum dist/ps5/akeno-ui.so > dist/ps5/akeno-ui.so.sha256
        ;;
    sdk)
        [[ $# == 2 && -f "$2" ]] || { printf '%s\n' 'Aufruf: bash build/build-ps5.sh sdk /pfad/zur/sdk.mk' >&2; exit 2; }
        make -f Makefile -f "$2" ps5
        ;;
    *)
        printf '%s\n' 'Aufruf: bash build/build-ps5.sh [module [CACHE] | sdk SDK_MAKEFILE]' >&2
        exit 2
        ;;
esac
