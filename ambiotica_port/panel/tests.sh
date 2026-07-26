#!/bin/sh
# Panel unit tests.
#
# panel.cpp is not compilable outside the amalgamation and the DSP harness never builds it,
# so the pure logic it depends on lives in small headers that are testable on their own:
# amalgamate.sh inlines them ahead of panel.cpp, these tests include them directly.
set -e
HN="$(cd "$(dirname "$0")" && pwd)"
CC="clang -O2 -std=c11 -Wall -Wextra -Werror"

for t in euclid stepcond; do
    $CC "$HN/${t}_test.c" -o "$HN/${t}_test"
    "$HN/${t}_test"
done
