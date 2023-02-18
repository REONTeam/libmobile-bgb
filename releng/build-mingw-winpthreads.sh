#!/bin/sh
set -e

# Build with support for Windows XP (yep)
rm -rf build-mingw-winpthreads
mkdir -p build-mingw-winpthreads
cd build-mingw-winpthreads
CC="x86_64-w64-mingw32-gcc -m32 -D_WIN32_WINNT=0x0501" \
../mingw-w64-v10.0.0/mingw-w64-libraries/winpthreads/configure \
    --prefix="$(realpath -m "$PWD/../lib/mingw-winpthreads")" \
    --host=x86_64-w64-mingw32 \
    --disable-shared
make
make install
