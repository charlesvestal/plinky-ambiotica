# Looper refill investigation (parked)

Branch: `refill-fix-investigation`, branched off `new-phrase-gesture`. This branch holds a
complete, verified fix for a real defect in `looper.c`'s write law, plus two traps that made it
hard to test and one side effect that isn't understood yet. The fix is NOT shipped - see
"Why this is parked" below. If you are picking this up, read this whole file before touching
anything; most of the time here went into telling a real result apart from a broken test.

## The original defect

`looper.c`'s per-sample write law (`looper_process`, around what was line 285 before this
branch):

```c
float in_g = 1.0f - fb_curr;
...
l->buf_L[pos] = st(soft_sat(in_g * in_l[n] + fb_curr * loopL));
```

`buf = (1-fb)*in + fb*old`, a normalised feedback blend. `fb` comes from `looper_set_layer`,
`layer^2` capped at 0.97. At LOOP 100%, `fb = 0.97`, so `in_g = 0.03`.

`looper_mark_clear` (via `drain_mark_clear`, see `drain.h`) makes `old` read as silence
instantly - that part already worked and is unrelated to this defect. But once `old` is
silence, `in_g` is the ONLY term left, and at LOOP 100% that term is 0.03. A cleared loop
refills at 3% per pass - roughly 33 passes to get back to level, which is minutes. A gesture
whose whole point is "start a new phrase now" cannot behave like that.

## The fix, and proof it works

`ambiotica_port/dsp/looper.c`: hoist the staleness check the read already does into
`const int stale_a`, then drive the write gain from it:

```c
const int stale_a = drain_stale(&l->drain, l->loop_len);
...
float in_g = stale_a ? 1.0f : (1.0f - fb_curr);
```

`stale_a` is true for exactly one `loop_len` after a clear (see `drain.h`), so this
self-terminates after one pass with no counter of its own, and needs no smoothing: at the
first non-stale sample, `old` is the full-level material the previous pass just wrote, so
`(1-fb)*in + fb*old` still evaluates to about `in`.

**This works.** Isolated `looper_process` alone (no chain), fed a 220 Hz / 0.12-amplitude
tone, `layer_0_1 = 1.0` (`fb` capped at 0.97), `loop_len = 64000` samples (2 s at 32 kHz):

```
                     sample 63999 (last stale sample)   sample 64000 (first non-stale sample)
unfixed (in_g=0.03):  peak 0.00000                        peak 0.00553   (built was 0.1737)
fixed:                 peak 0.00000                        peak 0.18334   (built was 0.1737)
```

At the first sample the loop can possibly show its own refilled content, the fixed version is
already back to ~105% of the pre-clear level; the unfixed version is at 3%. Exactly as
designed.

## Trap 1: born empty is not declared empty

`looper_create` calls `calloc`, so a freshly-created ring starts with `since_clear = 0` -
bit-identical to what `drain_mark_clear` produces. `drain_stale` (`age > since_clear`) cannot
tell "the user just cleared this" from "nothing has ever been recorded." Any caller that
treats staleness as a gesture (the `in_g` fix above, or anything read from
`looper_is_empty` below) fires at power-on and on every scene load too, for the length of one
`loop_len`, unless something seeds the ring as already-recorded.

Symptom before the fix for this: `eh_main.c`'s own "peak after 10 s of playing" clipped to
1.0000 - a fresh looper's first 10 s (with `loop_length_bars = 8`, `loop_len = 16 s`) were
being captured at full gain the whole time, not the intended ~0.19.

Fix: `drain.h` gained `drain_init_recorded(dc, cap)`, seeding `since_clear = cap` - fully
recorded, not stale, until an explicit `drain_mark_clear`. Called from `looper_create` and
`looper_reset` (with the ring capacity), and from `microloop_create` / `microloop_reset` for
the same reason and for symmetry.

Regression test: `ambiotica_port/panel/drain_test.c`,
`test_born_empty_is_not_declared_empty()` - a cursor seeded via `drain_init_recorded` is
not-stale for any age up to the seeded capacity, and only becomes stale after an explicit
`drain_mark_clear`. This is the test that stops someone "simplifying" the seed call back out
as a redundant zero-init.

## Trap 2: the measurement window

The harness test (`ambiotica_port/harness/np_main.c`) originally cleared the loop and then
measured `run(loop_len, 1)` - exactly one `loop_len` of samples - expecting to see the
refilled level.

It can't. `drain_stale` is `age > since_clear`, and `drain_tick` (which increments
`since_clear`) runs AFTER the per-sample stale check. So for a clear at sample 0, the read is
stale for samples `0 .. loop_len-1` and only stops being stale AT sample `loop_len` - one
sample past the last sample the exactly-`loop_len` window measures. Verified directly (see the
boundary-sample table above): both the fixed and unfixed looper are silent for the entire
measured window, so `np_main.c`'s "refilled" number was reading granular and micro-loop
residue and nothing of the looper's own output. It read the same value (bit-identical, not
just close) fixed and unfixed: `0.4473` of `0.6499` built (69%) either way. A test that can't
tell a fix from its absence is worse than no test - it looks green (or, here, consistently
FAIL in a way that had nothing to do with the code under test) regardless of what you do to
the code.

Fix: advance one full loop pass and DISCARD it, then measure the next stretch:

```c
run(loop_len, 1);
float refilled = run(SECS(0.25), 1);
```

With this, the test finally discriminates:

```
unfixed (in_g reverted, seed + looper_is_empty + corrected window all present): 35% (0.2269 / 0.6499)
fixed (this branch, as committed):                                             154% (1.0000 / 0.6499, clipped)
```

35% vs 154% (not 69% vs 69%) is the signature of a test that is actually looking at the right
samples.

Loose end: `refilled` hitting exactly `1.0000` means it is clipping - see Trap 3 - and 154% is
therefore a FLOOR, not a real number; the guard below only checks `built`, not `refilled`. A
second guard on `refilled` (or lowering the tone amplitude further) would be worth adding if
this is ever revived.

## Trap 3: the clipping trap

`FC_SOFT_CLIP` is only defined in `panel.cpp`. The desktop harness (`np_main.c`, `eh_main.c`)
never defines it, so `fc_render_block`'s output stage hard-clips at unity
(`lv = lv < -1 ? -1 : lv > 1 ? 1 : lv;`). The plan's first draft of `np_main.c` used a
0.4-amplitude tone with `mix = 1.0`, `loop_layer = 1.0`; between the loop bed's makeup gains
(`kLoopBedMakeup = 1.9`, `mk = 1 + 0.6*fb_curr`) and the granular shimmer, both the pre-clear
and post-clear levels exceeded 1.0 and clipped to exactly `1.0000` - so the refill ratio read
100% regardless of what the looper actually did, and the test passed even against completely
unfixed code. Lowering the tone amplitude to 0.12 fixed the steady-state case, but the fix
itself (see Trap 1) then pushed a NEW transient over the clipper during the loop's initial
fill, for an unrelated reason.

The part of this that shipped, and is worth keeping regardless of what happens to the rest of
this branch - it stays on `new-phrase-gesture` too:

```c
if (built > 0.90f) {
    printf("\nFAIL: built to %.4f, at or near the clipper - the measurement is blind.\n"
           "      Lower the tone amplitude in run() until this is comfortably under 0.9.\n",
           built);
    return 1;
}
```

This is the single most valuable thing this detour produced: a test that refuses to pass
vacuously against a hard-clipped signal, and says exactly what to do about it.

## The unresolved part - why this is parked

The `in_g` fix and the `since_clear` seed both work as designed (Trap 1, Trap 2 evidence
above). But they widen an existing mechanism (`drain_stale` on `l->drain`) that other code
also reads for reasons that have nothing to do with "how much gain does new input get" - and
one of those other readers produces an effect that is still not understood.

`eh_main.c` (an existing test predating this branch, unrelated to New Phrase, exercising
Event Horizon) measures the worst peak for 20 s after Horizon releases with no further input.
Original (nothing on this branch applied): `0.0078`, comfortably under its `0.02` limit. With
the `in_g` fix and the seed applied: `0.0238` - a 3x rise that puts it over the limit.

First hypothesis: drift regeneration. `full_chain.h`'s drift-regen block feeds the reverb wash
back into the looper's own input (`st->srcL`/`srcR`) even when the external input is silent.
During the post-clear stale window the fix now captures that regenerated signal at
`in_g = 1.0` instead of `~0.19` (eh_main.c runs Layer at 0.9, not 1.0) - a plausible ~5x
amplification of a signal the fix was never meant to touch. Added `looper_is_empty(l)` (new
predicate: `return l ? drain_stale(&l->drain, l->loop_len) : 0;`, declared in `looper.h`) and
gated `driftFbGain` to `0.0f` while the loop reads empty:

```c
const float driftFbGain = looper_is_empty(l) ? 0.0f
                        : 0.22f * p->drift_amt * (1.0f - 0.78f * p->decay) * (1.0f - 0.50f * p->spectra);
```

**This barely moved the number: `0.0238` to `0.0237`.**

Retraction, recorded honestly because it changed what the user decided: an earlier experiment
that set `p.drift_amt = 0.0f` for the entire `eh_main.c` run (not just the stale window)
showed a bigger drop (`0.0238 -> 0.0191`, both under the limit) and was read as support for
the drift-regen hypothesis. That experiment was not equivalent to gating regen while stale.
With the seed in place, a freshly-created looper is NOT stale (that is the whole point of
Trap 1's fix) - so during `eh_main.c`'s 10 s build-up phase, `looper_is_empty(l)` is false the
entire time and the properly-scoped gate leaves drift regen completely untouched there. Only
the blanket `drift_amt = 0` experiment silenced it during build-up too, which changed more of
the test than "gate while stale" does. The properly-scoped gate's real effect is much smaller
than that experiment suggested, and that mismatch is why it didn't work.

What's confirmed, precisely, about where the remaining 3x lives:
- `eh_main.c` phases 1 and 2 (`0.6741`, `0.2332`) match the ORIGINAL numbers exactly with the
  seed in place - the write-law fix is correctly scoped for everything up to the clear.
- Phase 3 (the 20 s release measurement) has NO external input, and with the regen gate
  applied, `driftFbGain` is genuinely `0.0f` for nearly all of it (the stale window from the
  clear runs 16 s on an 8-bar loop, covering the 2 s hold plus 14 of the 20 release seconds).
  So `in_g` is multiplying zero input for that whole stretch either way. The remnant change is
  therefore not explained by anything currently understood, which is exactly why it was not
  safe to ship.

Remaining candidate, not yet tested: `looper.c`'s Dilate reverse-head read
(`if (drain_stale(&l->drain, 2 * ph)) continue;`, in the reverse-head loop inside
`looper_process`) reads the SAME `l->drain` cursor that the seed and the `in_g` fix both
touch. `eh_main.c` runs with `p.dilate = 1.0`, so those heads are live throughout. Whether the
seed changes what they can read - independent of the `in_g` write-law change entirely - has
not been isolated. That is the next thing to check if this is revived.

## Why this is parked, in the user's words (relayed by the coordinator)

The refill fix is not the New Phrase feature - it was a repair noticed during design and
offered alongside it. It now has an unexplained 3x change in what leaks out of Event Horizon,
and shipping an unexplained change to hardware is worse than living with the slow bloom-in at
LOOP 100%, which is the instrument's existing, shipped behaviour. The investigation was
correct to keep stopping at each surprise rather than pushing a build that "passes" for
reasons nobody could account for.

## What's on this branch

- `ambiotica_port/dsp/drain.h` - `drain_init_recorded`.
- `ambiotica_port/dsp/looper.c` / `.h` - the `in_g` fix, the `stale_a` hoist, the
  `drain_init_recorded` calls in `looper_create`/`looper_reset`, `looper_is_empty`.
- `ambiotica_port/dsp/microloop.c` - the `drain_init_recorded` calls in
  `microloop_create`/`microloop_reset`.
- `ambiotica_port/harness/full_chain.h` - the `driftFbGain` gate.
- `ambiotica_port/harness/np_main.c` - the full end-to-end driver: the clipping guard, the
  corrected refill measurement (advance one pass, discard, then measure).
- `ambiotica_port/harness/build.sh` - builds and runs `amb_np_test`.
- `ambiotica_port/panel/drain_test.c` - `test_born_empty_is_not_declared_empty`.

`new-phrase-gesture` carries forward only the clipping guard and bare scaffolding in
`np_main.c` (allocator, `run()`, `peak()`, the params block) for Task 3 to build on; everything
else above stays here.

## If this is revived

1. Isolate the Dilate reverse-head hypothesis the same way the drift-regen one was isolated:
   run `eh_main.c` with `p.dilate = 0.0f` (seed + `in_g` fix + regen gate all still applied)
   and see whether the remnant returns to ~0.0078. If it does, the reverse-head read needs the
   same "gate while stale" treatment `driftFbGain` got. If it doesn't, the field is shared by a
   third reader nobody has found yet.
2. Add a `refilled`-side clipping guard to `np_main.c` (see Trap 3's loose end) before trusting
   any refill percentage above 100%.
3. Re-run this whole file's checklist before proposing a ship again: `eh_main.c`'s remnant back
   at ~0.0078 (not merely under the limit), `np_main.c`'s refill percentage using the corrected
   window, both guards clean, `sh ambiotica_port/panel/tests.sh` clean.
