# Ambiotica

A generative-ambient instrument. Play a few notes and let go - a rolling looper, a
granular scatter cloud, a freezing micro-loop, a Dattorro plate reverb and a bank of
tuned resonators bloom them into a slow wash that stays in key.

> **Designed for the Chords faceplate.** Every control sits under the printed word that
> describes it, so the overlay itself documents the panel. It runs on any Plinky 12 - on a
> different faceplate you just lose that correspondence.

Ambiotica owns the audio output: the built-in FX are silenced and the whole chain runs
on core 1.

## Play surface

The left half is eight strings tuned in **diatonic fourths** within the selected key and
mode, so any cluster is an open in-key chord and sliding up a string walks the scale.

## Macros (columns 8–14)

| | |
|---|---|
| **Orbit** | the rolling loop bed |
| **Satellite** | micro-loop; at the top it freezes into a held pad |
| **Constellate** | granular scatter - density and pitch spread |
| **Tail** | reverb decay |
| **Flux** | modulation depth and tank movement |
| **Spectra** | tuned resonators singing a chord out of the wash |
| **Mix** | dry/wet |

Orbit and Satellite carry a falling star marking their loop cycle; Constellate pulses
with grain activity.

## Column 15 - Gravity / Event Horizon

One bipolar slider, neutral at centre (white).

- **Up (green): Gravity** - collapse the engine into a slow evolving drone.
- **Down (red): Event Horizon** - drain everything back to silence for the next phrase.

## Drums

An 8-track x0x sequencer on its own page (**TRACKS**): the whole pattern at once, 8 tracks
down and 16 steps across. Kick, snare, closed and open hat, clap, rim and two toms -
synthesised at boot as 8-bit samples, so there are no files to install.

**Drag** across a row to write steps. Hold **×** (the printed shift key) and drag to scrub
them out again; hold **×** and tap **TRACKS** to clear the whole pattern.

### Generating - the printed GENERATE pads

Hold **PATTERN** and press a step to replace that track with a Euclidean rhythm of that
many pulses - press column 5 for five pulses, spread as evenly as sixteen steps allow.
Keep holding and slide along the row to rotate it, so tracks can be phased against each
other. PATTERN replaces; **FILL** builds up.

Hold **FILL** and tap a track to randomise a quarter of it; tap again to push it
further. **FILL + SYNTH** rolls a new sound out of the current preset.

### Conditions - the printed CONDITION pads

Both work the same way: the **first tap** on a step shows you what it is set to, in big
digits below the grid, without changing it. Tap that same step again to advance.

Hold **PROB** and tap a step to cycle how often it fires - 100 / 75 / 50 / 25% - also
shown as its brightness. Probability is what keeps a long ambient pattern from repeating
identically.

Hold **MODULO** and tap a step to make it play only every Nth time round: **1:2** is the
first time of every two, **2:2** the second, up to **4:4**. Two steps set to 1:2 and 2:2
trade off with each other. A step waiting its turn is dimmed on the passes it will not
fire, so you can watch a 1:4 breathe across four bars.

### Shuffle - the printed RHYTHM pad

Hold **RHYTHM** and the step grid becomes a shuffle setting: the **row** picks the style, the
**column** sets the depth, so one press sets both. The top row is straight; the seven below
are the Stolperbeats patterns Plinky's own sequencer uses, the last of which is plain 8th-note
swing. The chosen row fills to its depth like a bar, and the readout shows STR or S1..S7.

It works by warping the clock the sequencer reads rather than by nudging individual steps, so
it bends the whole grid at once and every track inherits it - including tracks on odd lengths,
which keep their own loop while swinging with everything else.

RHYTHM is where stock Chords keeps this too: its Shuffle sliders are reached by selecting the
Rhythm track.

### Length - polyrhythms

Hold **LENGTH** and press a step to set that track's loop length. Steps past the loop point
go dark, and keep their contents, so shortening and lengthening again loses nothing.

Give tracks different lengths and they drift: sixteen against seven only line up again
every 112 steps. Modulo follows each track's own loop, so "2nd of every 4" on a seven-step
track means four sevens.

Hold **MUTE** and tap a track to mute it; tap **MUTE** on its own to bring everything
back. Muted tracks keep showing their pattern, dimmed.

The kit deliberately sits **outside** the wash. Drums are mixed into the output after the
chain, so the looper, grains and plate never touch them: the pattern stays dry and legible
underneath whatever the ambience is doing. They also cost no polyphony - they are not synth
voices. Transport is on the printed ▷ and ▢ pads, available from every page.

## Dilate - play the bed backward

Tap **UNLOCK**, top right of the sequencer row, to reverse the loop bed and the micro-loop.
Two backward read heads half a window apart, Hann-crossfaded, so the pitch is unchanged and
there is no seam - the wash simply starts flowing the other way. It crossfades over about a
second.

The pad ramps **purple and rising** while reversed, the way reversed audio swells into a cut,
and sits **dim red and falling** while forward, like the ping and decay of a grain. Colour and
direction say the same thing, so either cue alone tells you the state, and it shows on every
page: a reversed bed sounds less like a loop and more like a swell, which is easy to mistake
for the looper having stopped.

UNLOCK earns this on the silkscreen: on stock Chords it is the pad that decides whether the
FWD / REV / RND / PING play direction applies to one track or to all of them, so it already
belongs to the play-direction family. It also sits directly above the column carrying Gravity
and Event Horizon, putting the three whole-engine gestures together.

## Control pads

| Pad | |
|---|---|
| **KEY** ▲▼ | move the key around the circle of fifths |
| **BANK** ▲▼ | mode: Ionian / Aeolian / Dorian / Lydian / Mixolydian |
| **SCALE** | back to the play surface |
| **PRESET** | synth preset browser |
| **SONG** | save/load a whole scene - every macro, the tuning and the sound |
| **SYNTH** | synth editor, laid out to match the printed Chords labels |

On the picker pages, **SAVE** and **LOAD** commit and **SCALE** cancels.

The pad for the page you are on breathes. No physical side button is claimed, so the
left pair still nudges BPM (Orbit and Satellite follow the tempo) and the right pair
still cycles pages.

## External audio - line in and mic

The chain will process anything you feed it, not just Plinky's own voices. Guitar, a drum
machine, a room mic: it all goes through the looper, the grains and the plate.

**Getting there:** page **up** with the right-hand side buttons from the play surface. The
first two pages are **src** and **in**; the left buttons change the value.

| | |
|---|---|
| **src** | `off` / `line` / `mic` - default `off`, synth only |
| **in** | input level, 0-127 |

Both are panel preferences, not part of a scene, so they survive a reboot.

**Start around 100 on `line`.** `in` reaches unity at 127 and tapers as a square law, so the
lower half of the range stays fine-grained for quiet sources; a line input at 40 will sound
like nothing is connected.

**`mic` needs a lower setting than you expect.** It shares the same control, and the chain
feeds back through the looper - a mic in the same room as the speaker will run away. Start
low and come up.

Event Horizon drains the *engine*, not the input: with `src` live, pulling the slider down
empties the loop but the incoming signal still passes through dry. Set `src` to `off` if you
want the bottom of the slider to be true silence.

## Credits

DSP vendored from [ambiotica-plugin](https://github.com/charlesvestal/ambiotica-plugin),
© Charles Vestal, MIT.
