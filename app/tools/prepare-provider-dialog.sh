#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Describe public dialog imports to the native linker. Never package these stubs.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bash "$root/tools/setup-native-dependencies.sh" >/dev/null
linker=$(command -v ld.lld-18 || command -v ld.lld)
compiler=$(command -v "${PS5_CLANG:-clang-18}")
out="$root/build/provider-dialog"
mkdir -p "$out"
for name in libSceCommonDialog libSceWebBrowserDialog; do
    source="$root/tooling/native/stubs/$name.S"
    if [[ ! -f "$out/$name.so" || "$source" -nt "$out/$name.so" || "$0" -nt "$out/$name.so" ]]; then
        "$compiler" --target=x86_64-unknown-freebsd -c "$source" -o "$out/$name.o"
        "$linker" -shared -soname "$name.so" "$out/$name.o" -o "$out/$name.so"
    fi
done
