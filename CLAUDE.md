# plinky-ambiotica — working notes

An ambient DSP engine (rolling looper → granular → micro-loop → Dattorro plate → tuned
"Spectra" resonators, plus Gravity / Event Horizon macros and an x0x drum machine) ported
to the Plinky 12 hardware synth as a custom panel.

## The one rule

**This panel is designed for the CHORDS faceplate, and the printed silkscreen is the spec.**

Never light a pad whose printed label says something else. The overlay is physical — a pad
keeps its label whatever page is showing, so a control under the right word documents
itself forever, and one under the wrong word lies. Read the manual before assigning a pad:

- https://plinky12.com/docs/chords/chords.md — what each label actually means
- `https://stage.plinky12.com/panel_art/chords.png` — the art (basic auth `p12` / `p12p12`)

Blocks and Toadstep (`docs/blocks/blocks.md`, `docs/toadstep/toadstep.md`) are the best
source for Plinky **grammar** — hold-a-modifier + tap-a-target, the same target list for
clear and randomise, tap-alone-to-unmute-all, `×` + up/down = reset to default — but their
pad *positions* are their silkscreens, not ours.

**A pad stays dark until it has a function that matches its label.**

## Layout

Chords draws pads only for rows 2..13; rows 0, 1, 14 and 15 are printed control rows. All
play UI lives in the middle 12 (`UI_Y` / `UI_H` in `panel.cpp`). Pages are a window onto a
taller surface — page N at logical `y = N*16`.

## Build and flash

```sh
sh ambiotica_port/panel/amalgamate.sh     # -> plinky_ambiotica.cpp (+ _profile, + dist/)
sh ambiotica_port/harness/build.sh        # desktop DSP harness; run it, it catches real errors
```

`panel.cpp` alone does not compile — it has no `#include`s and only type-checks after
amalgamation (the IDE injects SDK headers). Ignore clang "unknown type name looper_t" noise.

The generated file is **comment-stripped** (`strip_comments.py`): the flash endpoint rejects
uploads over ~200 KB and this project comments heavily on purpose. Sources keep every word.

CI publishes both builds to the `dist` branch on every push to `main`:

```
https://raw.githubusercontent.com/charlesvestal/plinky-ambiotica/<dist-sha>/plinky_ambiotica.cpp
https://raw.githubusercontent.com/charlesvestal/plinky-ambiotica/<dist-sha>/plinky_ambiotica_profile.cpp
```

The user flashes from those URLs. **Always hand over a dist-SHA-pinned URL, not a branch
URL** — pinned links stay valid after the next force-push.

**Flash via plinky12.com, not stage** — `printf` / Device Logs does not work on staging.

## Gotchas that cost real time

- **The desktop harness cannot measure CPU.** The RP2350 reaches big buffers through PSRAM
  behind a tiny XIP cache; things that are free on desktop cost 150 µs on device. Profile
  with the `_profile` build (`STG` line) and change one thing at a time.
- **Loading a panel kills the WebUSB log.** Firmware bug, reproducible on stock Chords, the
  instrument is unaffected. Do not re-debug it as a panel hang.
- **A panel's SD folder is created by SAVING**, not provisioned. An empty picker is an empty
  state, not a broken feature.
- **`setup_default_panel_state()` does not run on a staged load** — rebuild allocations in
  `on_load_finished()` or the DSP silently dies.
- **Never memset multi-MB PSRAM on core0**; it starves core1 through the shared QSPI bus.

## Memory

Durable findings live in this project's memory directory — start with
`plinky-ambiotica-open-items` for current state, and `plinky-chords-panel-design` before
touching any pad.
