#!/bin/sh
export CPPFLAGS="-UNDEBUG"
export CFLAGS="-Og -ggdb"
test "$(basename "$0")" = 'flags-debug-nosanit.sh' && exec "$@"
