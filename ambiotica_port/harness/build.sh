#!/bin/sh
# Build the Ambiotica desktop port harness in two variants:
#   amb_harness_float  — looper bed stored as float (original)
#   amb_harness_i16    — looper bed stored as int16 (-DLOOPER_I16)
# DSP .c files are compiled with allocation redirected to the instrumented
# bump allocator; main.c defines that allocator.
set -e
HN="$(cd "$(dirname "$0")" && pwd)"
DSP="$HN/../dsp"
cd "$HN"

REDIR="-Dcalloc=amb_calloc -Dmalloc=amb_malloc -Dfree=amb_free -Drealloc=amb_realloc -include $HN/amb_alloc.h"
CC="clang -O2 -std=c11 -w -I$DSP"

build_variant() {
    variant="$1"; extra="$2"
    od="obj_$variant"; rm -rf "$od"; mkdir -p "$od"
    for f in looper granular microloop reverb harmony drift bloom lfo dattorro; do
        $CC $REDIR $extra -c "$DSP/$f.c" -o "$od/$f.o"       # DSP (redirected alloc)
    done
    $CC $extra -c "$HN/main.c" -o "$od/main.o"               # driver (defines alloc)
    $CC "$od"/*.o -lm -o "amb_harness_$variant"
    echo "built: $HN/amb_harness_$variant"
}

build_variant float ""
build_variant i16   "-DLOOPER_I16"
