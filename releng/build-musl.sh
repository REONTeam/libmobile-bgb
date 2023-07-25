#!/bin/sh
set -e

rm -rf build-musl
mkdir build-musl
cd build-musl
CC="x86_64-multilib-linux-gnu-gcc -m32" \
AR="x86_64-multilib-linux-gnu-ar" \
RANLIB="x86_64-multilib-linux-gnu-ranlib" \
../musl-1.2.4/configure \
    --prefix="$(realpath -m "$PWD/../lib/musl")" \
    --disable-shared \
    --host=i686-unknown-linux-musl
make
make install
