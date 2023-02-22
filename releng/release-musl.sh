#!/bin/sh
set -e

rm -rf release-musl
mkdir -p release-musl
cd release-musl

. ../../tools/flags-release.sh
export CC="$PWD/../lib/musl/bin/musl-gcc -m32"
export REALGCC="x86_64-multilib-linux-gnu-gcc"
export LDFLAGS="-Wl,-static -Wl,-melf_i386"

../../configure --host=x86_64-multilib-linux-gnu \
	--without-system-libmobile
make
x86_64-multilib-linux-gnu-strip -s mobile
install -Dm755 mobile ../out/mobile-linux

cd ..
rm -rf release-musl
