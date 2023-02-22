#!/bin/sh
export CPPFLAGS="-UNDEBUG"
export CFLAGS="-Og -ggdb -fsanitize=address -fsanitize=leak -fsanitize=undefined"
test "$(basename "$0")" = 'flags-debug.sh' && exec "$@" || true
