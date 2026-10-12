#!/usr/bin/env bash
# Pinned official source, matching the desktop decoder. No downloaded binaries.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
src="$root/.deps/7zip"
archive="$root/.deps/7z2603-src.7z"
mkdir -p "$src"
if ! test -f "$archive"; then
  curl -fL --retry 3 https://github.com/ip7z/7zip/releases/download/26.03/7z2603-src.7z -o "$archive"
fi
printf '%s  %s\n' 41e2a7c0e9f838351c625e01f0f581a2188bb3dda10c8e0b4a852da26546ffe2 "$archive" | sha256sum -c -
if ! test -f "$src/CPP/7zip/Bundles/Format7zF/makefile.gcc"; then
  7z x -y -bso0 -bsp0 "-o$src" "$archive"
fi
target=${1:-host}
python3 "$root/tools/patch-7zip.py"
platform_flags=()
case "$target" in
  ps5) sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}; cc="$sdk/bin/prospero-clang"; cxx="$sdk/bin/prospero-clang++"; ar="$sdk/bin/prospero-ar"; objcopy="$sdk/bin/prospero-objcopy"; platform_flags=(ORBIT_PS5=1) ;;
  host) cc=clang-19; cxx=clang++-19; ar=ar; objcopy=llvm-objcopy-19 ;;
  *) echo 'Use host or ps5' >&2; exit 1 ;;
esac
out="$root/.deps/7zip-$target"
make -C "$src/CPP/7zip/Bundles/Format7zF" -f makefile.gcc -f "$root/tools/7zip-orbit.mak" \
  -j2 orbit-static O="$out" ST_MODE=1 CC="$cc" CXX="$cxx" AR="$ar" "${platform_flags[@]}" \
  CFLAGS_WARN_WALL='-Wall -Wextra' CXX_WARN_FLAGS= CFLAGS_BASE_LIST='-c -ffunction-sections -fdata-sections'
# libarchive embeds an older PPMd implementation with the same C symbol names.
# Keep both implementations private rather than mixing their incompatible state.
renames=()
for symbol in Alloc Construct DecodeSymbol Free Init MakeEscFreq Update1 Update1_0 Update2; do
  renames+=(--redefine-sym "Ppmd8_$symbol=Orbit7zip_Ppmd8_$symbol")
done
"$objcopy" "${renames[@]}" "$out/liborbit7zip.a"
