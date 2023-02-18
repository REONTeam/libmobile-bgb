#!/bin/sh
set -e

rm -rf dist
mkdir -p dist
cd dist

../../configure --without-system-libmobile
make distcheck
install -Dm644 -t ../out/ *.tar.gz

cd ..
rm -rf dist
