#!/bin/sh
set -e

rm -rf release-mingw
mkdir -p release-mingw
cd release-mingw

. ../../tools/flags-release.sh
export CC="x86_64-w64-mingw32-gcc -m32"
export LDFLAGS="-Wl,-static"

../../configure --host=x86_64-w64-mingw32 \
	--without-system-libmobile
make
x86_64-w64-mingw32-strip -s mobile.exe
install -Dm755 mobile.exe ../out/mobile-windows.exe

cd ..
rm -rf release-mingw
