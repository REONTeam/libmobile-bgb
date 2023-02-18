#!/bin/sh
set -e

rm -rf release-mingw
mkdir -p release-mingw
cd release-mingw

source ../../tools/flags-release.sh
export CC="x86_64-w64-mingw32-gcc -m32"
export CPPFLAGS="-isystem $PWD/lib/mingw-winpthreads/include"
export LDFLAGS="-Wl,-static -L$PWD/../lib/mingw-winpthreads/lib/"

../../configure --host=x86_64-w64-mingw32
make
x86_64-w64-mingw32-strip -s mobile.exe
install -Dm755 mobile.exe ../out/mobile-windows.exe

cd ..
rm -rf release-mingw
