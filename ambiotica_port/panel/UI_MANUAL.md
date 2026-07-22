# Ambiotica — Plinky panel · quick manual

An ambient instrument: you play the **built-in synth** on the left half of the
grid, and it flows straight into the **Ambiotica** ambient engine (rolling
looper → granular cloud → micro-loop → modulated reverb, with a tuned chord
resonator). Hold and release notes and the sound blooms into a slowly-evolving,
in-key wash. The right half is a bank of FX macros.

## The grid at a glance (16 × 16)

```
 columns 0 ───────────── 7 │ 8   9   10  11  12  13  14  15
        ┌─────────────────┐│ │   │   │   │   │   │   │
        │                 ││ O   C   S   T   F   S   M   (unused)
        │                 ││ r   o   a   a   l   p   i
        │   PLAY SURFACE  ││ b   n   t   i   u   e   x
        │  (built-in      ││ i   s   e   l   x   c
        │   synth)        ││ t   t   l       .   t
        │                 ││     e   l           r
        │  8 strings,     ││     l   i           a
        │  pitch rises ↑  ││     l   t           .
        │                 ││     a   e
        └─────────────────┘│ └──── FX macro sliders ────┘
```

- **Left (columns 0–7):** the play surface.
- **Right (columns 8–14):** seven vertical FX sliders, one per column.
- Column 15 is currently unused.

## Playing (left half)

- **8 strings** (the 8 columns). Pitch **rises as you go up** each column; strings
  are tuned a third apart, starting around **C3**, and everything is **locked to
  the current scale**, so it always sounds in-key.
- **Pressure-sensitive:** press harder for more level/expression. Up to **8 notes**
  at once.
- **Root notes glow brighter** (teal) so you can find your place.
- Playing feeds the ambient engine continuously — **let notes ring or lift them**
  and the wash keeps evolving underneath.

## FX macros (right half)

Each column 8–14 is a vertical slider — drag up/down. Bottom = min, top = max.

| Col | Macro | Turn up for… | What it does |
|:---:|-------|--------------|--------------|
| 8 | **Orbit** | a longer bed | Length & presence of the main rolling loop, ~½ bar → ~8 bars. Higher = longer and thicker, more slowly-evolving. |
| 9 | **Constellate** | shimmer | Granular texture. Low = clean; raising it overlaps the grains into a scattered, shimmering dust-cloud spread across the stereo field. |
| 10 | **Satellite** | → **Freeze** | Micro-loop length — a tight stutter that lengthens as you raise it; at the very **top it freezes** into a held, crystalline shimmer. |
| 11 | **Tail** | a bigger space | Reverb decay **and** chord-ring length — small room → vast cathedral. |
| 12 | **Flux** | movement | Adds detune "wow" and modulation, and feeds the reverb tail back into the loop so the texture **self-evolves** and drifts. |
| 13 | **Spectra** | a sung chord | The **chorded reverb**: tuned resonators ring the wash out into a sustained chord (currently **C minor** — see limitations). 0 = off. |
| 14 | **Mix** | wetter | Dry/wet balance between your raw playing and the ambient wash. |

## Quick start — an evolving pad

1. **Mix** (col 14) ~70%, **Tail** (col 11) high, **Spectra** (col 13) ~half.
2. Play a few notes on the left and **let go**.
3. Nudge **Orbit** up for a longer bed, **Flux** up to make it drift, **Satellite**
   to the top to **freeze** a held shimmer.

## Current limitations (scaffold)

- **Spectra chord is fixed to C minor.** Key/Chord selection isn't wired to the UI
  yet (planned for the side buttons or a settings page).
- **Gravity** (the plugin's one-gesture collapse into a frozen drone) isn't mapped yet.
- The **side buttons** and **column 15** are unused for now.
