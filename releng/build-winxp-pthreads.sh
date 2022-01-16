#!/bin/sh
set -e
CC="x86_64-w64-mingw32-gcc -m32 -D_WIN32_WINNT=0x0501" \
../mingw-w64-v9.0.0/mingw-w64-libraries/winpthreads/configure \
    --prefix="$(realpath "$PWD/../winxp-pthreads")" \
    --host=x86_64-w64-mingw32 \
    --disable-shared
make
make install
