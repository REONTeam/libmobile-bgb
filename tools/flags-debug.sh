#!/bin/sh
export CPPFLAGS="-UNDEBUG $CPPFLAGS"
export CFLAGS="-Og -ggdb -fsanitize=address -fsanitize=leak -fsanitize=undefined $CFLAGS"
test "$(basename "$0")" = 'flags-debug.sh' && exec "$@" || true
