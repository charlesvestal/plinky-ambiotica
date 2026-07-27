#!/usr/bin/env python3
"""Catch AMALGAMATION ORDER bugs before they reach the IDE.

amalgamate.sh strips every #include and relies purely on concatenation order, so a file that
uses a symbol defined in a later file compiles fine in the desktop harness (which honours
#include) and fails only in the Plinky web IDE. Neither tests.sh nor harness/build.sh can see
it. Two bugs of exactly this kind reached a flash attempt before this check existed:

  - hann.h called fast_cosf while sorted ahead of fast_math.h
  - draw_drums_page read mute_mod one line before declaring it

The generated file cannot be compiled here, since the SDK headers are injected by the IDE, so
this does not try. It runs the parser anyway and keeps only the undeclared identifiers that
are also DECLARED somewhere in the same file - those are ours, and being undeclared at the
point of use means they are in the wrong order. SDK symbols are declared nowhere in the file
and drop out on their own.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
src = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "plinky_ambiotica.cpp"
if not src.exists():
    sys.exit(f"check_order: no {src} - run amalgamate.sh first")

out = subprocess.run(
    ["clang++", "-fsyntax-only", "-w", "-ferror-limit=0", "-std=c++17", str(src)],
    capture_output=True, text=True,
).stderr

# "file:LINE:col: error: use of undeclared identifier 'foo'"
uses = {}
for m in re.finditer(r":(\d+):\d+: error: use of undeclared identifier '(\w+)'", out):
    uses.setdefault(m.group(2), int(m.group(1)))

text = src.read_text().splitlines()

TYPES = r"(?:void|int|float|double|char|short|long|bool|unsigned|signed|size_t|auto|const|static|inline|struct|enum)"

def declared_at(sym):
    """Line number where this file declares `sym`, or None. Deliberately strict: a mere
    mention is not a declaration, or every SDK call would look locally defined."""
    pats = [
        re.compile(r"^\s*#\s*define\s+" + re.escape(sym) + r"\b"),
        # function or global at column 0: `static inline float fast_cosf(` / `void foo(`
        re.compile(r"^(?:" + TYPES + r"[\w \t*&]*?)\b\*?" + re.escape(sym) + r"\s*[(\[=]"),
        # local declaration inside a function: `bool mute_mod = ...` / `const int x;`
        re.compile(r"^\s+(?:" + TYPES + r"[\w \t*&]*?)\s\*?" + re.escape(sym) + r"\s*[=;\[]"),
        re.compile(r"^\s*typedef\b.*\b" + re.escape(sym) + r"\s*;"),
        re.compile(r"^\s*\}\s*" + re.escape(sym) + r"\s*;"),   # typedef struct { ... } foo;
    ]
    for i, line in enumerate(text, 1):
        if any(p.search(line) for p in pats):
            return i
    return None

bad = []
for sym, use_line in sorted(uses.items(), key=lambda kv: kv[1]):
    d = declared_at(sym)
    if d is not None and d > use_line:
        bad.append((sym, use_line, d))

if not bad:
    print(f"order: no amalgamation-order problems in {src.name}")
    sys.exit(0)

for sym, use_line, dec_line in bad:
    print(f"ORDER BUG: '{sym}' used at line {use_line}, declared at line {dec_line}")
    print(f"    use:     {text[use_line - 1].strip()[:96]}")
    print(f"    declare: {text[dec_line - 1].strip()[:96]}")
sys.exit(1)
