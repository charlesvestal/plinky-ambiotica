#!/bin/sh
# Unit test for the Euclidean generator.
#
# panel.cpp is not compilable outside the amalgamation, and the DSP harness never builds it,
# so euclid.h is kept separately testable: the amalgamation inlines it ahead of panel.cpp and
# this test includes it directly.
set -e
HN="$(cd "$(dirname "$0")" && pwd)"
clang -O2 -std=c11 -Wall -Wextra -Werror "$HN/euclid_test.c" -o "$HN/euclid_test"
"$HN/euclid_test"
