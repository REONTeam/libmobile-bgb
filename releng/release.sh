#!/bin/sh
set -e

make_mingw() {
    CC="x86_64-w64-mingw32-gcc -m32" \
    CFLAGS="-isystem '$PWD/lib/winxp-pthreads/include'" \
    LDFLAGS="-L '$PWD/lib/winxp-pthreads/lib'" \
    ../make-mingw -C .. "$@"
}

make_musl() {
    CC="$PWD/lib/musl/bin/musl-gcc -m32" \
    REALGCC="x86_64-multilib-linux-gnu-gcc" \
    LDFLAGS="-static -Wl,-melf_i386" \
    make -C .. "$@"
}

if command -v x86_64-multilib-linux-gnu-gcc 2>&1 > /dev/null; then
    make_musl clean
    make_musl optim
    cp ../mobile mobile-linux
    make_musl clean
fi

if command -v x86_64-w64-mingw32-gcc 2>&1 > /dev/null; then
    make_mingw clean
    make_mingw optim
    cp ../mobile.exe mobile-windows.exe
    make_mingw clean
fi
