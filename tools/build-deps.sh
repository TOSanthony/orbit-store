#!/usr/bin/env bash
set -euo pipefail
mkdir -p /tmp/orbit-deps
cd /tmp/orbit-deps
fetch() {
  if ! test -f "$2"; then curl --silent --show-error --fail --location --retry 3 "$1" -o "$2"; fi
  printf '%s  %s\n' "$3" "$2" | sha256sum -c -
  tar xf "$2"
}
fetch https://github.com/openssl/openssl/releases/download/openssl-3.5.2/openssl-3.5.2.tar.gz openssl.tar.gz c53a47e5e441c930c3928cf7bf6fb00e5d129b630e0aa873b08258656e7345ec
fetch https://curl.se/download/curl-8.18.0.tar.xz curl.tar.xz 40df79166e74aa20149365e11ee4c798a46ad57c34e4f68fd13100e2c9a91946
fetch https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz microhttpd.tar.gz a89e09fc9b4de34dde19f4fcb4faaa1ce10299b9908db1132bbfa1de47882b94
fetch https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz zlib.tar.gz bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
fetch https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz bzip2.tar.gz ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
fetch https://github.com/tukaani-project/xz/releases/download/v5.8.1/xz-5.8.1.tar.gz xz.tar.gz 507825b599356c10dca1cd720c9d0d0c9d5400b9de300af00e4d1ea150795543
fetch https://github.com/libarchive/libarchive/releases/download/v3.8.1/libarchive-3.8.1.tar.xz libarchive.tar.xz 19f917d42d530f98815ac824d90c7eaf648e9d9a50e4f309c812457ffa5496b5
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
export CPPFLAGS="-I$PS5_SYSROOT/user/homebrew/include"
export LDFLAGS="-L$PS5_SYSROOT/user/homebrew/lib"
cd /tmp/orbit-deps/openssl-3.5.2
./Configure BSD-x86_64 no-tests no-apps no-shared --prefix="$PREFIX"
make -j4 build_sw
make install_sw DESTDIR="$PS5_SYSROOT"
cd /tmp/orbit-deps/curl-8.18.0
./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared --with-openssl="$PS5_SYSROOT/user/homebrew" --without-zlib --without-brotli --without-zstd --without-libpsl --without-libidn2 --without-libssh2 --disable-ldap --disable-ldaps --disable-docs --disable-manual --disable-ftp --disable-file --disable-dict --disable-gopher --disable-imap --disable-mqtt --disable-pop3 --disable-rtsp --disable-smb --disable-smtp --disable-telnet --disable-tftp
make -j4
make install DESTDIR="$PS5_SYSROOT"
cd /tmp/orbit-deps/libmicrohttpd-1.0.1
./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared --disable-doc --disable-curl --disable-examples --disable-https
make -j4
make install DESTDIR="$PS5_SYSROOT"
# Archive extraction: ZIP (deflate, bzip2, LZMA), 7z (LZMA/LZMA2, PPMd, BCJ) and RAR4/RAR5 readers only.
cd /tmp/orbit-deps/zlib-1.3.2
CHOST=x86_64-pc-freebsd ./configure --prefix="$PREFIX" --static
make -j4 libz.a
make install DESTDIR="$PS5_SYSROOT"
cd /tmp/orbit-deps/bzip2-1.0.8
make -j4 libbz2.a CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="-O2 -D_FILE_OFFSET_BITS=64"
install -m 644 bzlib.h "$PS5_SYSROOT$PREFIX/include/"
install -m 644 libbz2.a "$PS5_SYSROOT$PREFIX/lib/"
cd /tmp/orbit-deps/xz-5.8.1
./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared --disable-xz --disable-xzdec --disable-lzmadec --disable-lzmainfo --disable-lzma-links --disable-scripts --disable-doc --disable-nls --disable-microlzma --disable-lzip-decoder
make -j4
make install DESTDIR="$PS5_SYSROOT"
cd /tmp/orbit-deps/libarchive-3.8.1
./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared --disable-bsdtar --disable-bsdcpio --disable-bsdcat --disable-bsdunzip --disable-acl --disable-xattr --without-openssl --without-xml2 --without-expat --without-iconv --without-lz4 --without-zstd --without-lzo2 --without-nettle --without-mbedtls --without-cng --without-libb2 --with-zlib --with-bz2lib --with-lzma
make -j4
make install DESTDIR="$PS5_SYSROOT"
