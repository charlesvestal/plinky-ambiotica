# Ambiotica

Play a few notes and let go. A rolling looper catches them, a granular cloud scatters them, a
plate reverb smears the result, and a bank of tuned resonators picks a chord back out of it.
Underneath all that, an 8-track drum machine.

> **Built for the Chords faceplate.** Every control sits under the printed word for it, so the
> overlay helps to orient you to the controls. 

## Hear it

- [Ambiotica on Plinky 12](https://www.youtube.com/watch?v=NG1DBZ1N4b0), the engine on its own
- [Now with Drums](https://www.youtube.com/watch?v=jAwj6go3ipw), the sequencer running underneath
- [Live Input Demo](https://www.youtube.com/watch?v=nSFfK28sIks), external audio through the chain

## Playing

The left half is eight strings in diatonic fourths, inside whatever key and mode you have
picked. Grab a few at once and you get an open chord. Slide up one string and you walk the
scale.

**KEY** ▲▼ moves around the circle of fifths. **BANK** ▲▼ changes mode: Ionian, Aeolian,
Dorian, Lydian, Mixolydian.

## The wash

Seven macro sliders, columns 8 to 14.

| | |
|---|---|
| **Orbit** | length of the rolling loop bed |
| **Satellite** | micro-loop length. At the top it freezes into a held pad |
| **Constellate** | granular scatter: how many grains, how far they wander in pitch |
| **Tail** | reverb decay |
| **Flux** | modulation depth and tank movement |
| **Spectra** | tuned resonators, singing a chord out of the wash |
| **Mix** | dry/wet |

Orbit and Satellite each drop a falling star on their loop cycle. Constellate pulses with
grain activity. Flux modulates the Tail column alongside itself, so you can see what it is
reaching for.

## Gravity and Event Horizon

Column 15 is one bipolar slider. Centre is neutral, and white.

**Up, green: Gravity.** The engine collapses into a slow drone.

**Down, red: Event Horizon.** Everything drains away. At the bottom the loop is empty and the
next phrase starts from silence.

## Dilate

Tap **UNLOCK**, top right of the sequencer row, and the loop bed and micro-loop run backward.
Two read heads half a window apart, crossfaded. Pitch does not change and there is no seam. It
takes about a second to turn around.

## Drums

Its own page, on **TRACKS**: eight tracks down, sixteen steps across, the whole pattern visible
at once. Kick, snare, closed and open hat, clap, rim, two toms. They are synthesised at boot as
8-bit samples, so there is nothing to install.

**Drag** across a row to write steps. Hold **×** (the printed shift key) and drag to rub them
out. Hold **×** and tap **TRACKS** to wipe the lot.

The kit sits outside the wash. Drums are mixed in *after* the ambient chain, so the looper and
the plate never touch them, and the pattern stays dry and legible under whatever is going on
above it. They are not synth voices either. They cost no polyphony.

Transport is on the printed ▷ and ▢ pads and works from any page.

### Generating: PATTERN and FILL

Hold **PATTERN** and press a step to replace that track with a Euclidean rhythm. Press column 5
and you get five pulses, spread as evenly as sixteen steps allow. Keep holding and slide along
the row to rotate it. That is how you phase tracks against each other.

PATTERN replaces. **FILL** builds up: hold it and tap a track to randomise a quarter of it, tap
again to push it further.

**FILL + SYNTH** rolls a new sound out of the current preset.

### Conditions: PROB and MODULO

Both behave the same way. The **first tap** on a step only shows you what it is set to, in big
digits under the grid. Tap it again to change it.

Hold **PROB** and tap a step to set how often it fires: 100, 75, 50, 25%. Brightness shows it
too. This is what stops a long pattern from repeating itself identically.

Hold **MODULO** and tap a step to make it play only every Nth time round. **1:2** is the first
pass of every two, **2:2** the second, up to **4:4**. Set two steps to 1:2 and 2:2 and they
trade off with each other. A step that is not firing this pass goes dim, so you can watch a 1:4
come round.

### Shuffle: RHYTHM

Hold **RHYTHM** and the grid turns into a shuffle setting. The **row** picks the style, the
**column** picks the depth. One press sets both.

The top row is straight. The seven below it are the Stolperbeats patterns Plinky's own
sequencer uses, and the last of those is ordinary 8th-note swing. The chosen row fills to its
depth like a bar, and the readout shows STR, or S1 to S7.

### Length: LENGTH

Hold **LENGTH** and press a step to set that track's loop length. Steps past the loop point go
dark but keep their contents. Shorten a track and lengthen it again and nothing is lost.

Give tracks different lengths and they drift apart. Sixteen against seven will not line up
again for 112 steps. Modulo counts each track's own loop, so "2nd of every 4" on a seven-step
track means four sevens.

Hold **MUTE** and tap a track to mute it. Tap **MUTE** on its own to bring everything back.
Muted tracks still show their pattern, dimmed.

## Getting around

| Pad | |
|---|---|
| **SCALE** | back to the play surface |
| **TRACKS** | drums |
| **SYNTH** | synth editor, laid out to match the printed Chords labels |
| **PRESET** | synth preset browser |
| **SONG** | save or load a whole scene: every macro, the tuning, the sound |

Whichever page you are on, its pad pulses. On the pickers, **SAVE** and **LOAD** commit and
**SCALE** cancels.

No side button is claimed. The left pair still nudges BPM, which Orbit and Satellite follow,
and the right pair still cycles pages.

## Line in and mic

The chain will process live input, not just Plinky's own voices. Guitar, a drum machine, a room
mic, try different inputs!

Page **up** from the play surface with the right-hand side buttons. The first two pages are
**src** and **in**. The left buttons change the value.

| | |
|---|---|
| **src** | `off` / `line` / `mic`. Default `off`, synth only |
| **in** | input level, 0 to 127 |

There is a [demo of this](https://www.youtube.com/watch?v=nSFfK28sIks) if you want to hear what
it does to a live signal.

## Credits

DSP from [ambiotica-plugin](https://github.com/charlesvestal/ambiotica-plugin),
© Charles Vestal, MIT.

This panel was built with help from coding agents like Claude, but with significant design,
oversight and hours from a human (me). If that's not to your taste, totally fine!
