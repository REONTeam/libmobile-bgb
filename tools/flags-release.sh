#!/bin/sh
export CFLAGS="-Os -flto -fuse-linker-plugin"
test "$(basename "$0")" = 'flags-release.sh' && exec "$@"
