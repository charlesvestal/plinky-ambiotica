# New Phrase: hold `×` + `REC`

A gesture that empties the loop buffers on contact and leaves the reverb ringing, so you can
start a new phrase over the tail of the old one. Event Horizon already clears the buffers, but
only at the end of a slider ride that collapses the whole engine first. This is the same
emptiness without the collapse.

## The pad

**`REC`, col 12 row 15**, the printed record circle, immediately left of `×` at col 13 row 15.
Dark at rest, lit only while `×` is held. Free on every page: nothing in `panel.cpp` touches
col 12 row 15, and `COL_SAVE = 12` is on row 14.

Verified 2026-08-03 against the current `plinky12.com/panel_art/chords.png`, which is now
hardware accurate. Row 15 reads `▼×7` (cols 0-6), `CHORD RELATED PAGES SONG SYNTH` (cols
7-11), then `⭕ × ▢ ▷` (cols 12-15).

This is stock Chords grammar rather than a repurpose. From `chords.md`:

> "× is a shift key that lets you reset or delete individual things across all of Plinky 12
> Chords if you hold it and tap one of the other pads."

and, in the MIDI chord-record flow:

> "Alternatively, you can tap the **X** pad to wipe your recording."

So "`×` + the record pad wipes the recording" is already what `×` means on this faceplate, and
on this panel the rolling looper genuinely is the recording. The pads are adjacent, so it is a
one-handed move in the bottom-right corner.

Known and accepted: on stock Chords, `REC` alone is also the randomise modifier (hold Rec and
tap a fader, the XY pad, or the arp pattern). We put randomise on `FILL`. We claim only
`× + REC`; `REC` alone stays dark, so nothing collides. Whether reroll should move to `REC` is
a separate question and out of scope here.

## Behaviour

| Phase | What happens |
| --- | --- |
| Contact | `looper_mark_clear` + `microloop_mark_clear` on the next audio block. The bed is silent from that instant |
| Held, < 900 ms | Re-stamp the clear every block, so the buffer stays empty while the tail rings |
| Held, > 900 ms | The plate's decay and its input send lerp toward zero over 1200 ms |
| Release | Recording resumes from empty; decay returns to its Tail setting |

A stab clears the loop. A deliberate press and wait, about 2.1 s, is a full stop.

"On contact" means the first audio block after the UI thread sees the touch: at most one block,
`BLOCK_SIZE` 64 at `AMB_SR` 32000, so 2 ms. That is the same responsiveness as every other pad
on the instrument and nothing in the panel can beat it. **The clear is never delayed for the
sake of declicking** - see "The edge, not the action" below.

## What is cleared, and what rings

Reading `fc_render_block` in `harness/full_chain.h`, the chain is:

```
looper -> granular ---------------\
                                   +--> dattorro (plate) -> harmony -> drift -> mix
bloom  -> microloop --------------/
```

Only `looper_mark_clear` and `microloop_mark_clear` are called. Everything else follows:

- **`granular`** self-clears within a buffer. It only reads the looper.
- **`bloom`** is fed by the live dry input, so it holds nothing of the old phrase.
- **`harmony` (Spectra) runs on the plate's wet output.** "Let the plate ring" and "let Spectra
  ring" are therefore the same decision, and resetting harmony while keeping the plate would cut
  the resonators dead inside a room that is still sounding.
- **`drift`** is a short modulated delay on the final bus. Whatever it holds is gone in its own
  delay time.
- **Drums** are rendered post-chain and are untouched. `× + TRACKS` already owns clearing those.

So `harmony_reset` / `bloom_reset` / `drift_reset` stay exclusive to Event Horizon, where total
collapse is the point. This gesture is two calls.

## Collapsing the tail on a long hold

`full_chain.h:235-240` records the fact that governs this:

> "Decay alone cannot silence a Dattorro - decay is the tank's feedback, not its input gain, so
> at decay 0 it still emits one pass of diffused input."

So the long hold drives **one scalar, `tail_kill` (0..1), into two places**:

1. the reverb input, `rin *= (1 - tail_kill)`, folded into the existing `rin` loop so there is
   no extra pass over the block;
2. the decay, `t *= (1 - tail_kill)` before `dattorro_set_decay`.

**It deliberately does not duck the wet output**, which is what Event Horizon does at
`full_chain.h:252-254`. Ducking would mean a partial hold hides a tail that is still in the
tank, and releasing would swell it back. Collapsing decay and the send instead means the tank
genuinely empties: everything you heard was real, a partial hold leaves a genuinely shorter
tail, and release cannot resurrect anything. It also stops the gesture sounding like a second
Event Horizon and starts it sounding like the room shrinking, which is what it is.

On release `tail_kill` returns to 0 through a one-pole of about 50 ms, so the reverb send does
not step back to unity. Against a 1200 ms fade that lag is negligible.

## The refill fix

The looper writes `buf = (1 - fb) * in + fb * old` (`looper.c:279-285`) with `fb = LOOP²` capped
at 0.97 (`looper.c:210-220`). After a clear, `old` reads as 0, so the input enters at `1 - fb`:

| LOOP | `fb` | input gain | passes to full level |
| --- | --- | --- | --- |
| 50% | 0.25 | 0.75 | ~1 |
| 80% | 0.64 | 0.36 | ~3 |
| 100% | 0.97 | 0.03 | ~33 |

At the top of the LOOP range the clear is instant and total and then the new phrase blooms in
over tens of loop passes, which for a gesture called "new phrase" reads as broken. This is the
current Event Horizon release behaviour too, not something this gesture introduces.

The fix reuses machinery already on that line. `drain_stale` is already evaluated for the read,
and it is true for exactly one `loop_len` after a clear:

```c
float in_g = stale ? 1.0f : (1.0f - fb_curr);
```

Take the input at full gain precisely while there is nothing to mix it with. It self-terminates
after one loop pass and needs no counter.

The handover is click-free without smoothing, and the arithmetic is worth keeping written down:
at the first non-stale sample, `old` is the full-level material the previous pass just wrote, so
`(1 - fb) * in + fb * old` still evaluates to about `in`. There is no step to smooth. From the
second pass on, the loop content is the normal high-feedback blend.

This also repairs the same slow bloom on Event Horizon's release. The panel is unreleased and
single-user, so changing that behaviour costs nothing.

## The edge, not the action

Marking the buffer clear takes the bed from full level to zero in one sample, which is a step
and therefore a click. The obvious fix, fading the bed out and clearing at the bottom, delays
the action, and the whole point of the gesture is that it is immediate.

So the clear stays on the first available sample and the **edge** is smoothed instead. At the
cut, seed a stereo residual from the previous block's final `lay + mic` sample and decay it to
zero with a one-pole of about 1.5 ms, summing it into `wbL/wbR`. The step becomes a 5 ms slope.
Nothing is delayed, because the residual is a correction added on top of an output that is
already silent.

The residual is kept out of the reverb send. Five milliseconds of decaying residual into a
diffusion network is inaudible, and keeping it on the output bus alone is simpler.

Cost: four floats in `fc_state` and one multiply-add per sample for about 160 samples.

## Where the state lives

**`tail_kill` and the edge residual go in `fc_state`, not `full_params`.**

`fc_render_block` memcmps `full_params` to gate `fc_push_params`
(`full_chain.h`, the `changed`/`push_ctr & 3` test). A float moving across a 1200 ms fade would
clear that gate on roughly every fourth block, turning a slow visual fade into several hundred
`powf`/`expf` coefficient re-pushes on the RP2350. In `fc_state` the panel writes them directly
each block and the gate never sees them.

## Implementation

**New file `ambiotica_port/panel/newphrase.h`** - pure logic, no SDK types, per the standing
rule that new panel logic lives in a tested header rather than inline in `panel.cpp`:

```c
typedef struct { unsigned held_us; unsigned char active; } newphrase_t;

void  newphrase_press(newphrase_t *np);
void  newphrase_release(newphrase_t *np);
void  newphrase_tick(newphrase_t *np, unsigned dt_us);
int   newphrase_stamp(const newphrase_t *np);       /* 1 while held: keep the buffers empty */
float newphrase_tail_kill(const newphrase_t *np);   /* 0..1 target, DSP one-poles toward it */
```

Constants: `NP_TAIL_HOLD_US = 900000`, `NP_TAIL_FADE_US = 1200000`.

**`harness/full_chain.h`**: `tail_kill` into the `rin` loop and the decay; the `in_g` refill fix
lands in `dsp/looper.c`; the edge residual into the `wb` sum.

**`panel/panel.cpp`**: add `COL_REC = 12` to the `CTL_DN` group with the manual quote as its
comment. Drive `newphrase_tick` from the UI thread, consume `newphrase_stamp` and
`newphrase_tail_kill` in `on_dsp` beside the existing `eh_flushed` block.

**`amalgamate.sh`** inlines `newphrase.h` ahead of `panel.cpp`; **`tests.sh`** builds
`newphrase_test`.

Nothing is serialised. The gesture is momentary and carries no scene state.

## LEDs and help text

The long hold is a time-based mode, so it has to be visible rather than remembered:

| State | `REC` pad |
| --- | --- |
| `×` not held | off |
| `×` held, not touched | `DIMMER(RED)` |
| Gesture active, before the threshold | `RED` |
| Past the threshold | `fade_col(RED, 255 * (1 - tail_kill))` |

Past the threshold the pad darkens in step with the tail it is collapsing, reaching black at the
moment the tail is gone. Help text is "New phrase - clear the loop, keep the tail", switching to
"New phrase - clearing the tail too" at the threshold.

## Testing

**`newphrase_test.c`**, in the pattern of `stepcond_test.c` and `drain_test.c`:

- press sets `stamp == 1` and `tail_kill == 0`
- held to 899 ms, `tail_kill` is still 0
- held to 900 + 600 ms, `tail_kill == 0.5`
- held to 900 + 1200 ms and beyond, `tail_kill == 1.0`, clamped
- release clears `stamp` and returns the target to 0

**Harness test**, extending `harness/eh_main.c`:

- the bed is silent within the edge window and stays silent while held
- **the plate is still ringing right after the clear.** This is the feature; it is the
  assertion that fails the day someone adds a plate reset for tidiness
- the plate falls below a floor after a long hold
- the bed is back at full level one `loop_len` after release, even at `fb = 0.97`, which pins
  the refill fix

Driven through `fc_render_block` with a persistent `fc_state`. **Never by looping `fc_render`**,
which calls `fc_init` and resets the chain including the dattorro pointer, so such a test passes
vacuously.

## Risks

- **`×` may drop when the second pad is first triggered.** Recorded as unconfirmed against
  `× + PRESET`, seen on the simulator only, and `shift_sticky` is *not* in `panel.cpp` today, so
  nothing guards against it. `REC` is adjacent to `×`, which is the best case for reach, but the
  gesture should be tried on hardware early. If it misfires, that is the known bug, not this
  design.
- **CPU.** Two extra multiplies per sample on `rin`, one on the decay per block, and a
  multiply-add for 5 ms after a cut. Expected to be free, but the desktop harness cannot measure
  CPU: confirm on the `_profile` build via the `STG` line.
- **Arena.** A handful of bytes of panel state plus five floats in `fc_state` (the four for the
  edge residual, plus `tail_kill`). The two figures
  recorded for free arena disagree with each other, so read the actual `PANEL: free=` print
  rather than either.

## Out of scope

- Moving reroll from `FILL` to `REC`.
- Any change to Event Horizon's own slider behaviour, other than the refill fix it inherits.
- Clearing the drum pattern, which `× + TRACKS` already does.
