#!/usr/bin/env bash
# Host tests link the same pinned libarchive as the PS5 payload, so both
# report archives (encryption, damage, links) the same way.
set -euo pipefail
mkdir -p /tmp/orbit-deps
cd /tmp/orbit-deps
if ! test -f libarchive.tar.xz; then curl --silent --show-error --fail --location --retry 3 https://github.com/libarchive/libarchive/releases/download/v3.8.1/libarchive-3.8.1.tar.xz -o libarchive.tar.xz; fi
printf '%s  %s\n' 19f917d42d530f98815ac824d90c7eaf648e9d9a50e4f309c812457ffa5496b5 libarchive.tar.xz | sha256sum -c -
rm -rf host-libarchive && mkdir host-libarchive && tar xf libarchive.tar.xz -C host-libarchive --strip-components=1
cd host-libarchive
CC=clang-19 ./configure --prefix=/usr/local --disable-shared --enable-static --with-pic --disable-bsdtar --disable-bsdcpio --disable-bsdcat --disable-bsdunzip --disable-acl --disable-xattr --without-openssl --without-xml2 --without-expat --without-iconv --without-lz4 --without-zstd --without-lzo2 --without-nettle --without-mbedtls --without-libb2 --with-zlib --with-bz2lib --with-lzma
make -j4
make install
