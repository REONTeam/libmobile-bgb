#!/bin/sh
set -e

( cd ..; autoreconf -vsi )
./dist.sh
if command -v x86_64-multilib-linux-gnu-gcc 2>&1 > /dev/null; then
    ./release-musl.sh
fi
if command -v x86_64-w64-mingw32-gcc 2>&1 > /dev/null; then
    ./release-mingw.sh
fi
