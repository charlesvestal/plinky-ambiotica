#!/bin/sh
# Build the Ambiotica desktop port harness in two variants:
#   amb_harness_float  - looper bed stored as float (original)
#   amb_harness_i16    - looper bed stored as int16 (-DLOOPER_I16)
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
    for f in looper granular microloop harmony drift bloom lfo dattorro; do
        $CC $REDIR $extra -c "$DSP/$f.c" -o "$od/$f.o"       # DSP (redirected alloc)
    done
    $CC $extra -c "$HN/main.c" -o "$od/main.o"               # driver (defines alloc)
    $CC "$od"/*.o -lm -o "amb_harness_$variant"
    echo "built: $HN/amb_harness_$variant"
}

build_variant float ""
build_variant i16   "-DLOOPER_I16"

# Event Horizon end-to-end check. Same objects as the i16 harness (the device variant), a
# different driver: play, drain, release, and assert the loop does not come back.
od="obj_i16"
$CC -DLOOPER_I16 -c "$HN/eh_main.c" -o "$od/eh_main.o"
$CC "$od"/looper.o "$od"/granular.o "$od"/microloop.o "$od"/harmony.o \
    "$od"/drift.o "$od"/bloom.o "$od"/lfo.o "$od"/dattorro.o "$od"/eh_main.o \
    -lm -o "amb_eh_test"
echo "built: $HN/amb_eh_test"
"$HN/amb_eh_test"

# New Phrase end-to-end check. Same objects again, a third driver: clear the loop mid-play and
# assert it refills in one pass, that the plate keeps ringing, and that a long hold collapses it.
$CC -DLOOPER_I16 -c "$HN/np_main.c" -o "$od/np_main.o"
$CC "$od"/looper.o "$od"/granular.o "$od"/microloop.o "$od"/harmony.o \
    "$od"/drift.o "$od"/bloom.o "$od"/lfo.o "$od"/dattorro.o "$od"/np_main.o \
    -lm -o "amb_np_test"
echo "built: $HN/amb_np_test"
"$HN/amb_np_test"
