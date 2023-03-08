#!/bin/sh
export CFLAGS="-Os -flto -fuse-linker-plugin $CFLAGS"
test "$(basename "$0")" = 'flags-release.sh' && exec "$@" || true
