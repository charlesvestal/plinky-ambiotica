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

On the **play surface**, hold **×** and tap **PRESET** to toggle **Dilate**. The pad's lower
printed word is **REV**, one of the stock play-direction group, and that is exactly what this
does: the loop bed and the micro-loop swap to backward read heads. Two heads half a window
apart, Hann-crossfaded, so the pitch is unchanged and there is no seam - the wash simply
starts flowing the other way. It crossfades in over about a second.

**The pad ramps for as long as Dilate is on**, from any page: a slow **green** sawtooth that
**rises**, the way reversed audio swells into a cut. Hold × on the play page with Dilate off
and it is **orange** and **falls** instead, like the ping and decay of a grain played
forward. Colour and direction say the same thing, so either one alone tells you the state. The ramp draws
the envelope you are hearing, so the shape reads before the colour does. A reversed bed
sounds less like a loop and more like a swell, and without that light it is easy to mistake
for the looper having stopped.

Only the play page toggles it, so holding × to erase drum steps cannot flip it by accident.

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

## Settings

Right-up from the play surface: **src** (off / line / mic) routes external audio into
the chain, and **in** sets its level. Default is off - synth only.

## Credits

DSP vendored from [ambiotica-plugin](https://github.com/charlesvestal/ambiotica-plugin),
© Charles Vestal, MIT.
