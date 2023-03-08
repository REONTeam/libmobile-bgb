#!/bin/sh
export CPPFLAGS="-UNDEBUG $CPPFLAGS"
export CFLAGS="-Og -ggdb $CFLAGS"
test "$(basename "$0")" = 'flags-debug-nosanit.sh' && exec "$@" || true
