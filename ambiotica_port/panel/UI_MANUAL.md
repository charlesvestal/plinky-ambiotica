# Ambiotica — Plinky panel · quick manual

An ambient instrument: you play the **built-in synth** on the left half of the grid, and
it flows straight into the **Ambiotica** engine (rolling looper → granular cloud →
micro-loop → Dattorro plate reverb, with a tuned chord resonator). Hold and release notes
and the sound blooms into a slowly-evolving, in-key wash. The right half is a bank of FX
macros, and the last column plus the side buttons shape the whole engine.

## The grid at a glance (16 × 16)

```
 columns 0 ───────────── 7 │ 8   9   10  11  12  13  14 │ 15
        ┌─────────────────┐│ O   C   S   T   F   S   M  │ Gravity  (up)
        │                 ││ r   o   a   a   l   p   i  │   ▲
        │   PLAY SURFACE  ││ b   n   t   i   u   e   x  │   ·  centre = neutral
        │  (built-in      ││ i   s   e   l   x   c      │   ▼
        │   synth)        ││ t   t   l       .   t      │ Event Horizon (down)
        │  8 strings      ││     e   l           r      │
        └─────────────────┘│ └──── FX macro sliders ────┘
```

- **Left (columns 0–7):** the play surface.
- **Right (columns 8–14):** seven vertical FX sliders, one per column.
- **Column 15:** the bipolar Gravity / Event Horizon control.

## Playing (left half)

- **8 strings** (the 8 columns), pitch rising up each column.
- Each string is a **chord tone** stacked up in octaves, so **playing across strings
  voices the selected chord** and sliding along one string moves through the key's scale.
  Everything stays **in key**.
- **Pressure-sensitive:** press harder for more level. Up to **8 notes** at once.
- **Root notes glow brighter**; the whole surface recolors with the key and chord.
- Playing feeds the ambient engine continuously — **let notes ring or lift them** and the
  wash keeps evolving underneath.

### Key & chord (side buttons)

- **Left pair** steps the **key** around the circle of fifths (up = up a fifth).
- **Right pair** cycles the **chord**: min / maj / sus4 / 5th / oct.

Both retune the play surface *and* the Spectra chord, and recolor the surface (hue = key,
shade = chord).

## FX macros (right half)

Each column 8–14 is a vertical slider — drag up/down. Bottom = min, top = max.

| Col | Macro | Turn up for… | What it does |
|:---:|-------|--------------|--------------|
| 8 | **Orbit** | a longer bed | Length & presence of the main rolling loop, ~½ bar → ~8 bars. Higher = longer, thicker, more slowly-evolving. |
| 9 | **Constellate** | shimmer | Granular texture. Low = clean; raising it overlaps grains into a scattered, shimmering, pitch-spread cloud. |
| 10 | **Satellite** | → **Freeze** | Micro-loop length — a tight stutter that lengthens as you raise it; near the **top it freezes** into a held pad. |
| 11 | **Tail** | a bigger space | Reverb decay **and** chord-ring length — small room → vast cathedral (~1.3 s → ~12 s). |
| 12 | **Flux** | movement | Adds detune "wow" and reverb-tank modulation, and feeds the reverb tail back into the loop so the texture **self-evolves**. |
| 13 | **Spectra** | a sung chord | The **chorded reverb**: tuned resonators ring the wash out into the selected chord. 0 = off. |
| 14 | **Mix** | wetter | Dry/wet balance between your raw playing and the ambient wash. |

## Column 15 — Gravity / Event Horizon

A bipolar slider; **centre = neutral**.

- **Up = Gravity** — the master "collapse into the drone." Pushes the whole engine toward
  its ambient extreme: the loop bed swells, reverb maxes, the micro-loop freezes, the chord
  rings, grains thicken. Ramps in slowly (~2 s).
- **Down = Event Horizon** — the "let go." Drains the loop, micro-loop, chord, and reverb
  tail back toward silence; at the very bottom it clears the buffers so you start fresh.

## Quick start — an evolving pad

1. **Mix** (col 14) ~70%, **Tail** (col 11) high, **Spectra** (col 13) ~half.
2. Play a few notes on the left and **let go**.
3. Nudge **Orbit** up for a longer bed, **Flux** up to make it drift, **Satellite** to the
   top to **freeze** a held pad.
4. Push **column 15 up** to collapse it all into a drone; pull it **down** to drain and
   start a new phrase.
