#!/bin/sh
set -e
CC="x86_64-pc-linux-gnu-gcc -m32" \
AR="x86_64-pc-linux-gnu-ar" \
RANLIB="x86_64-pc-linux-gnu-ranlib" \
../musl-1.2.3/configure \
    --prefix="$(realpath $PWD/../musl)" \
    --disable-shared \
    --host=i686-pc-linux-musl
make
make install
