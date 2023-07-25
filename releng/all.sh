#!/bin/sh
set -e

tar xf src/musl-1.2.4.tar.gz
./build-musl.sh
rm -rf build-musl musl-1.2.4

exec ./release.sh
