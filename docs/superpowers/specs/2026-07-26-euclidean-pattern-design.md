# Euclidean generation on the printed PATTERN pad

**Date:** 2026-07-26
**Status:** approved, ready for implementation planning
**Scope:** `ambiotica_port/panel/panel.cpp`, drums page only

## Summary

Hold the printed **PATTERN** pad and press a step in a drum track to replace that track with
a Euclidean rhythm whose pulse count is the column you pressed. Keep holding and slide left
or right to rotate the result. One pad, one continuous gesture, two numbers.

## Why PATTERN

PATTERN is at **row 1, column 13**, inside the printed **GENERATE** group, immediately
beside FILL at column 14 — which already carries our reroll. GENERATE on stock Chords is
literally "styles on how to fill the sequence", so an ordered generator belongs there as
plainly as the random one does.

The two then read as a deliberate pair with opposite characters:

| pad | group | character | effect on the track |
|-----|-------|-----------|---------------------|
| FILL (col 14) | GENERATE | random, cumulative — a quarter per press | builds up |
| PATTERN (col 13) | GENERATE | ordered, deterministic | replaces |

**Position verified against the user's own faceplate on 2026-07-26**, not against
`panel_art/chords.png`. That PNG is a stale revision: it prints `SHUFFLE` at row 1 col 9
where hardware prints `MELODY`, and `6 7 8 9` at row 0 cols 8-11 where hardware prints
`FWD REV RND PING`. The run `MODULO PROB PATTERN FILL UNLOCK` at cols 11-15 is identical in
both, so the existing `COL_PROB = 12` and `COL_REROLL = 14` bindings are sound and PATTERN
is confirmed at col 13.

## 1. Gesture

A new modifier predicate reading column 13 on `CTL_TOP2`, exactly parallel to the existing
`reroll_held()` / `prob_held()` at `panel.cpp:280-281`.

**Press** — step `s`, track row `t`, PATTERN held, `drum_paint == 0`:

- latch `drum_paint = 3`, the established "modifier used" value, which blocks the paint path
- pulse count `k = s + 1`; column 0 gives 1 pulse, column 15 gives all 16
- store `eu_track = t`, `eu_anchor = s`, `eu_k = k`, `eu_last = s`
- generate into track `t` at rotation 0

**Drag** — while still held, when the finger reaches a different column `s'` **in row
`eu_track`**:

- `rot = (s' - eu_anchor) & 15`
- regenerate track `eu_track` with `eu_k` at `rot`
- touches on any other row are ignored for the life of the gesture, so a vertical wander
  cannot hijack a second track

**Release** — the existing `if (!any_down) drum_paint = 0` at `panel.cpp:789` tears the
gesture down; `eu_track` is reset to `-1` in the same place so a stale gesture cannot resume
against a track the finger has already left.

**Note on the latch.** Every other modifier on this page acts once per hold via a plain
`if (!drum_paint)` guard. PATTERN cannot use that guard alone, because the drag must keep
acting while `drum_paint == 3`. Its branch instead uses two distinct conditions: the press
fires on `drum_paint == 0`, and the drag fires on `eu_track >= 0 && t == eu_track &&
s != eu_last`. Implementing this with the plain once-only guard would give pulse selection
and no rotation.

There is no way to generate zero pulses; an empty track is what `×` erase and `× + TRACKS`
are already for.

### Modifier precedence

The chain at `panel.cpp:749` becomes: mute → reroll → **pattern** → prob → paint. PATTERN
and FILL are siblings in GENERATE; if both are held at once, FILL wins. Arbitrary, but
fixed and documented rather than emergent.

## 2. Generator

Pure, no scratch array, no recursion, no allocation:

```c
/* Bresenham/bucket form. Maximal evenness is what a Euclidean rhythm IS, so this lands on
   the same necklace as Bjorklund without the machinery. Positive rot moves pulses later,
   so sliding the finger right pushes the pattern right. */
static void euclid_fill(unsigned char* dst, int k, int rot) {
    for (int i = 0; i < DRUM_STEPS; i++) {
        int j = (i - rot) & (DRUM_STEPS - 1);
        dst[i] = ((j * k) % DRUM_STEPS) < k ? 127 : 0;
    }
}
```

`DRUM_STEPS` is 16 (`ambiotica_port/dsp/drums.h:25`), so `& (DRUM_STEPS - 1)` is a valid
mask — the same idiom already used for `drum_step` at `panel.cpp:354`.

`k = 5` yields pulses at 0, 4, 7, 10, 13 — spacings 4, 3, 3, 3, 3.

Pulses are written at **127** (always fires), which is what hand-painting writes at
`panel.cpp:765`. Rests are written as 0.

## 3. Integration

**No serialisation change.** Rotation is baked into `pattern[]` at generate time rather than
kept as a live parameter, so the existing `FIELD_BASE64` round-trip in `on_serialise`
already covers the result. No scene-format change and no version bump.

**No new locking.** Generation runs in `on_ui` on core0; `on_sequence` reads `pattern[]` from
the high-priority timer. This is the identical risk profile as painting and reroll, which
already write `pattern[]` from `on_ui` — worst case one step reads a stale byte for one
16th, which is inaudible.

**PROB composes.** Generated steps sit at 127 and PROB can dial any of them down afterward.
Regenerating the track wipes those edits, which is the intended "replace" semantics.

**Mute is orthogonal** — it gates triggering in `fire_drum_step`, not pattern contents.

## 4. Feedback

- The PATTERN pad is dim when idle and bright while held, mirroring `panel.cpp:510-511`.
  Colour **GREEN** — distinct from FILL's PURPLE and PROB's CYAN, and already used in the
  file.
- The grid redraws from `pattern[]` every frame, so rotation animates live under the finger
  with no additional draw code.
- Help line reads `Euclid 5/16 +2` while the gesture is active.

## 5. Cost

Four bytes of gesture state (`eu_track`, `eu_anchor`, `eu_k`, `eu_last`) plus one ~6-line
function. Against roughly 3.8 KB free in the 128 KB panel arena this is negligible. No new
buffers, no PSRAM, no allocation.

## 6. Testing

**The desktop harness cannot test this.** `ambiotica_port/harness/build.sh` compiles
`dsp/*.c` plus `main.c` only — `panel.cpp` is never built there, and it does not compile
standalone in any case (no `#include`s; it only type-checks after amalgamation).

Therefore:

- `euclid_fill` gets a small standalone clang test, written **before** the implementation:
  - for every `k` in 1..16, the output contains exactly `k` pulses. This holds for every `k`,
  including those sharing a factor with 16: as `i` runs over `Z16`, `i*k mod 16` hits each
  multiple of `g = gcd(k,16)` exactly `g` times, and `k/g` of those multiples fall below
  `k`, giving `(k/g) * g = k`. Worth asserting for all 16 values rather than spot-checking,
  since `k = 6` and `k = 10` are where a naive generator drifts.
  - rotation is a pure shift: rotating by `r` equals rotating by `r + 16`
  - `k = 16` fills every step; `k = 1` yields a single pulse
  - `rot` moves the pulse set right, verified against a known case
- The gesture itself is verified on hardware after flashing.

## Out of scope

- Storing pulses/rotation as live per-track parameters. Deliberately rejected: it would
  require serialisation work and would make the generator own the track, preventing hand
  editing afterward.
- Rotating a hand-drawn pattern without regenerating it. A consequence of the press-to-place
  gesture; revisit only if it proves annoying in use.
- Euclidean on anything but the 8 drum tracks.
