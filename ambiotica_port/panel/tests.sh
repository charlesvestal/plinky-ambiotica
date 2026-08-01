#!/bin/sh
# Panel unit tests.
#
# panel.cpp is not compilable outside the amalgamation and the DSP harness never builds it,
# so the pure logic it depends on lives in small headers that are testable on their own:
# amalgamate.sh inlines them ahead of panel.cpp, these tests include them directly.
set -e
HN="$(cd "$(dirname "$0")" && pwd)"
CC="clang -O2 -std=c11 -Wall -Wextra -Werror"

for t in stepcond mipmap steptime; do
    $CC "$HN/${t}_test.c" -o "$HN/${t}_test"
    "$HN/${t}_test"
done

# drain.h is DSP rather than panel logic, but it is pure integer arithmetic over a ring and
# the desktop harness cannot show what it is for: whether a held Event Horizon actually
# silences the loop. Needs the dsp/ include path.
$CC -I"$HN/../dsp" "$HN/drain_test.c" -o "$HN/drain_test"
"$HN/drain_test"

# Amalgamation-order check, if a build has been generated. Catches use-before-definition in
# the concatenated panel, which no other check here can see.
if [ -f "$HN/plinky_ambiotica.cpp" ] && command -v clang++ >/dev/null 2>&1; then
    python3 "$HN/check_order.py" "$HN/plinky_ambiotica.cpp"
fi
