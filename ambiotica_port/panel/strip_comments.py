#!/usr/bin/env python3
"""Strip C/C++ comments from the amalgamated panel.

The Plinky flash endpoint rejects large uploads ("Request too large"), and this project
comments heavily on purpose: comments and blank lines were ~45% of a 200 KB generated
file, which was enough to be refused. The sources keep every word; only the build
artifact is stripped.

This walks the file tracking string, char and comment state rather than using a regex,
so a "//" inside a string literal — a URL, a printf format — is never mistaken for a
comment. Getting that wrong would corrupt the panel silently.

The caller is responsible for emitting the @-metadata block separately: the panel
library parses the FIRST block comment in the file, so it must survive stripping.
"""
import re
import sys


def strip(src: str) -> str:
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c in '"\'':                      # string / char literal — copy verbatim
            q = c
            out.append(c)
            i += 1
            while i < n:
                if src[i] == '\\':          # escape: take both chars, so \" can't end it
                    out.append(src[i:i + 2])
                    i += 2
                    continue
                out.append(src[i])
                if src[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            i += 2
            while i + 1 < n and not (src[i] == '*' and src[i + 1] == '/'):
                i += 1
            i += 2
            out.append(' ')                 # a space, so `a/*x*/b` stays two tokens
            continue
        out.append(c)
        i += 1
    txt = '\n'.join(line.rstrip() for line in ''.join(out).split('\n'))
    return re.sub(r'\n{2,}', '\n', txt).lstrip('\n')


if __name__ == '__main__':
    with open(sys.argv[1], encoding='utf-8', errors='replace') as f:
        sys.stdout.write(strip(f.read()))
