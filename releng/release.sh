#!/bin/sh
set -e

make_mingw() {
    CC="x86_64-w64-mingw32-gcc -m32" \
    CFLAGS="-isystem '$PWD/winxp-pthreads/include'" \
    LDFLAGS="-L '$PWD/winxp-pthreads/lib'" \
    ../make-mingw -C .. "$@"
}

make_musl() {
    CC="$PWD/musl/bin/musl-gcc -m32" \
    REALGCC="x86_64-pc-linux-gnu-gcc" \
    LDFLAGS="-static -Wl,-melf_i386" \
    make -C .. "$@"
}

make_musl clean
make_musl
make_mingw clean
make_mingw
