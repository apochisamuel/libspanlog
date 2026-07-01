#!/bin/bash
set -e

cd "$SRC"

# Build library with ASAN
make CC=$CC CFLAGS="$CFLAGS -Iinclude -Isrc" clean all

# Build fuzz harnesses
for fuzzer in stream_fuzzer channel_fuzzer session_fuzzer migrate_fuzzer; do
    $CC $CFLAGS -Iinclude -Isrc -c fuzz/${fuzzer}.c -o fuzz/${fuzzer}.o
    $CXX $CXXFLAGS $LIB_FUZZING_ENGINE fuzz/${fuzzer}.o -L. -lspanlog -o $OUT/${fuzzer}
done
