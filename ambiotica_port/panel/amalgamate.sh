#!/bin/sh
# Amalgamate the Ambiotica port into ONE #include-free .cpp for the Plinky web
# IDE. Order: LOOPER_I16 define -> arena allocator (+ calloc redirect) -> module
# headers -> full_chain -> module implementations -> panel. All #include lines
# are stripped (Plinky auto-injects the standard headers; #include is banned).
set -e
HN="$(cd "$(dirname "$0")" && pwd)"
DSP="$HN/../dsp"
HARN="$HN/../harness"

# Build stamp for the generated header: which commit + when it was generated, so
# a downloaded drop-in shows its provenance (CI fills these on every push).
# Falls back gracefully outside a git checkout.
STAMP_HASH="$(git -C "$HN" rev-parse --short HEAD 2>/dev/null || echo nogit)"
git -C "$HN" diff --quiet HEAD 2>/dev/null || STAMP_HASH="${STAMP_HASH}-dirty"
STAMP_TIME="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

# AMB_OUT lets the profiling pass below write to a different file.
OUT="${AMB_OUT:-$HN/plinky_ambiotica.cpp}"

strip() { grep -vE '^[[:space:]]*#[[:space:]]*include' "$1"; }

MODULES="looper granular microloop harmony bloom drift lfo dattorro drums"

# Body first, into a temp file, so it can be comment-stripped before the metadata block is
# prepended. The flash endpoint rejects large uploads ("Request too large") and this project
# comments heavily on purpose - comments were ~45% of a 200 KB file, enough to be refused.
# The sources keep every word; only this artifact is stripped.
BODY="$(mktemp)"
trap 'rm -f "$BODY"' EXIT
{
    strip "$HN/amb_config.h"          # build config (LOOPER_I16); per-module flags live in their .c files
    [ -n "$AMB_PROFILE" ] && echo "#define AMB_PROFILE"
    strip "$HN/alloc_prelude.h"
    for h in $MODULES; do strip "$DSP/$h.h"; done
    strip "$DSP/mipmap.h"             # used by drums.c, so it must precede it
    strip "$DSP/rate_util.h"
    strip "$DSP/drain.h"              # Event Horizon sweep cursor, used by looper.c + microloop.c
    strip "$DSP/fast_math.h"
    strip "$DSP/hann.h"               # AFTER fast_math.h (needs fast_cosf), before its users
    strip "$HARN/full_chain.h"
    for c in $MODULES; do strip "$DSP/$c.c"; done
    strip "$HN/stepcond.h"
    strip "$HN/newphrase.h"           # x + REC hold timer; inlined ahead of panel.cpp
    strip "$HN/panel.cpp"
} | python3 "$HN/strip_comments.py" /dev/stdin > "$BODY"

{
    # Panel library metadata. MUST be the first BLOCK comment in the file - that is what
    # the panel library parses (plinkysynth/community-panels), so it is emitted here rather
    # than passing through the comment stripper, which would eat it.
    #
    # @Author and @Firmware are the required fields. @Artwork is banned for community
    # submissions (ship artwork.png beside the .cpp instead). @Category is free text and is
    # displayed but not used for grouping or filtering, so it buys little.
    #
    # @Firmware is the firmware base to build against: `latest`, a channel (`beta` / `alpha` /
    # `release`), or a four-character build code pinning a specific base. `c019` is the pin the
    # community-panels MAINTAINER chose for us in their commit f4e5074, and it is kept rather
    # than moved to `latest` because they can only bless artifacts for firmware listed in
    # Plinky's versions.json. Do not change it without asking them.
    #
    # THIS BLOCK IS THE ONLY SOURCE OF TRUTH FOR THE SUBMITTED HEADER. @Video was once added
    # by hand to the copy living in community-panels (their commit 680d7e9) and not here, so
    # the next regeneration silently deleted it from the submission. Anything the panel page
    # should show has to live in this heredoc or it does not survive a rebuild.
    #
    # @Version is NOT one of the documented fields (the list is @Name, @Author, @Description,
    # @Documentation, @Video, @Tags, @Preferred Panels, @Discussion, @Category, @Level) and no
    # other community panel sets it. The parser reads @-fields generically, so it costs one
    # line and reads fine in the source either way. Nothing bumps it automatically: the build
    # stamp below tracks commits, this tracks releases, and only a human moves it.
    #
    # KEEP THIS SHORT. Free prose after the @-fields is the fallback for panels that do not
    # set @Description ("a plain-text description can go here if you do not use
    # @Description"), so with @Description set it is not shown at all - it was five dead
    # paragraphs duplicating README.md, which IS displayed, on the panel cover. It also came
    # out of the flash budget, which this file is already managed against.
    cat <<'META'
/*
@Name: Ambiotica
@Author: Charles Vestal
@Firmware: c019
@Version: 1.1
@Description: Play a few notes and let go. A rolling looper, a granular cloud, a plate reverb and a bank of tuned resonators turn them into a slow wash, with an 8-track drum machine running dry underneath.
@Preferred Panels: chords
@Tags: ambient, generative, looper, granular, reverb, drone, drums, sequencer
@Documentation: https://github.com/charlesvestal/plinky-ambiotica
@Discussion: https://github.com/charlesvestal/plinky-ambiotica/issues
@Video: https://www.youtube.com/watch?v=NG1DBZ1N4b0
*/
META
    echo "// GENERATED by amalgamate.sh - single-file Plinky panel. Edit the sources, not this."
    echo "// build ${STAMP_HASH}  generated ${STAMP_TIME}"
    echo "// Ambiotica-on-Plinky. Vendored DSP from ambiotica-plugin, (c) Charles Vestal - MIT (see LICENSE)."
    echo "// Comments stripped to fit the flash upload limit - the commented sources are in the repo."
    cat "$BODY"
} > "$OUT"

echo "wrote $OUT ($(wc -l < "$OUT" | tr -d ' ') lines, $(wc -c < "$OUT" | tr -d ' ') bytes)"

# Amalgamation-order check. This file strips every #include and depends purely on the order
# above, so a use-before-definition compiles fine in the harness and fails only in the IDE.
# Non-fatal: it needs clang, and a missing clang must not stop a build from being produced.
if command -v clang++ >/dev/null 2>&1; then
    python3 "$HN/check_order.py" "$OUT" || echo "amalgamate: ORDER PROBLEM ABOVE - this WILL fail in the IDE" >&2
fi

# LEXICAL check. The order check above does NOT compile anything, and nothing else here does
# either - panel.cpp has no #includes, so the only real compiler is the IDE, on the far side of
# an upload. That let a wrecked comment ship: prose written after a */ became code, "2026-08-07"
# lexed as an octal constant, an apostrophe opened a character literal, and a member declaration
# inside the wreckage vanished. Every error the IDE reported was LEXICAL, and lexical errors
# need no headers to find - so find them here, where the loop is seconds instead of an upload.
# Deliberately narrow: only diagnostics that cannot be caused by the missing SDK headers.
if command -v clang++ >/dev/null 2>&1; then
    # -ferror-limit=0 is REQUIRED, not tidiness. Without headers clang hits its default 20-error
    # limit inside the first 30 lines (size_t, the SDK types) and never reaches the real fault
    # thousands of lines below. The first version of this check was silent for exactly that
    # reason. The IDE reports these as errors under -Werror; clang calls them warnings, so match
    # on the text rather than the severity.
    lex=$(clang++ -fsyntax-only -std=c++17 -ferror-limit=0 "$OUT" 2>&1 \
          | grep -Ei "invalid digit|missing terminating|unterminated|invalid suffix" | head -20)
    if [ -n "$lex" ]; then
        echo "$lex" >&2
        echo "amalgamate: LEXICAL ERRORS ABOVE - this WILL fail in the IDE (check comment delimiters)" >&2
    fi
fi

# Submission bundle in the plinkysynth/community-panels layout. Built locally every time
# so it never drifts from the sources; submitting is a separate, manual act.
#
#   dist/<author>/README.md            author gallery page
#   dist/<author>/<key>/<key>.cpp      the panel - exactly one .cpp, directly in the dir
#   dist/<author>/<key>/README.md      shown on the IDE panel cover once the panel is opened
#   dist/<author>/<key>/artwork.png    square library thumbnail
#
# The panel ID is <author>/<key>, so BOTH slugs must be lowercase letters, digits and
# underscores only. The panel directory takes exactly one .cpp and no subdirectories, so this
# rebuilds the tree from scratch rather than adding to it. The library listing comes from the
# @-metadata block at the top of the .cpp, not from README.md.
# The submission bundle is built only for the normal panel, not the profiling variant.
if [ -n "$AMB_OUT" ]; then exit 0; fi

AUTHOR=charlesvestal
KEY=ambiotica
DIST="$HN/dist/$AUTHOR/$KEY"
rm -rf "$HN/dist/$AUTHOR"
mkdir -p "$DIST"
cp "$OUT"                   "$DIST/$KEY.cpp"
cp "$HN/library_readme.md"  "$DIST/README.md"
cp "$HN/artwork.png"        "$DIST/artwork.png"
cp "$HN/author_readme.md"   "$HN/dist/$AUTHOR/README.md"
echo "wrote $HN/dist/$AUTHOR/ (README.md, $KEY/{$KEY.cpp,README.md,artwork.png})"

# Profiling variant, built alongside so it can never drift from the real panel. It prints
# per-stage core1 timings (STG loop/gran/mic/rev/harm/mix/push/drum), which is what decides
# where PLINKY_DSP_RAM_FUNC is worth spending the limited SRAM code region on. Deliberately
# NOT in dist/ - that directory allows exactly three files - but published beside the normal
# build so it can be flashed from a URL like any other.
# AMB_OUT guards the recursion: the child sets it, so it builds the body and stops.
if [ -z "$AMB_OUT" ]; then
    AMB_PROFILE=1 AMB_OUT="$HN/plinky_ambiotica_profile.cpp" sh "$0"
fi
