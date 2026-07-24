# Plinky 12 — System Notes for Panel Development

Working reference for building **panels** on the Plinky 12. Compiled from the
public panel API dump (`llm.txt`) and direct answers from the Plinky devs
(**mmalex**, **makingsoundmachines**) on Discord, 2026-07-22 — several of which
are **undocumented** and marked ⭐.

---

## 1. Hardware

| | |
|---|---|
| MCU | **RP2350** (dual ARM Cortex-M33), the Raspberry Pi Pico 2 chip |
| PSRAM | **8 MB** external QSPI (shared scratch for panels) |
| Display | **16×16 RGB LED grid** — this is the *entire* screen |
| Input | Every pad is **pressure-sensitive** (full multitouch) + 4 side buttons + **accelerometer** |
| Audio | Stereo in/out, 32 kHz, 64-sample DSP blocks |
| I/O | TRS MIDI in + 2 TRS MIDI out, USB MIDI, CV/clock/reset, **SD card** |

The device display **is** the 16×16 LED grid. There is no higher-res screen.
The "second-screen"/emulator web view only shows help text, not a framebuffer.

---

## 2. Execution model (cores & hooks)

A panel is a C++ class derived from `panel_t`. Override only the hooks you need.

| Hook | Core / context | Rate | Use for |
|---|---|---|---|
| `on_ui(int dt_us)` | core0, foreground | ~250 fps | draw the 16×16 view, read widgets/touch. **Can pause** during SD I/O. |
| `on_sequence(...)` | core0, **IRQ context** | ~500–1000 fps | musical timing: playheads, notes, voice alloc. Keep short & deterministic; no SD / big memory. |
| `on_dsp_voices(...)` | **core1** | per 64-sample block | audio only; fill `mix_buffers`. |
| `on_click / on_touch / on_midi` | core0 | event | side buttons / raw pad edges / incoming MIDI |
| `setup_default_panel_state()` | core0 | once | build default song/panel state after settings load |
| `on_serialise / on_serialise_settings` | core0 | save/load | durable state / durable prefs (JSON) |

⭐ **Compute budget (mmalex):** *"go for your life on core0 — it only blocks UI."*
Heavy sustained work in `on_ui` is fine; it just stalls UI refresh, **not audio**
(audio is core1). `on_sequence` is core0 but IRQ context.
⭐ **core1 is DSP only.** You cannot run game/render logic there.

---

## 3. Memory model

| Region | Size | Notes |
|---|---|---|
| Panel object arena | **128 KB** | your whole `panel_t` subclass instance + members (mutable state) live here. Always yours. |
| Second 128 KB (shadow) | 128 KB | ⭐ borrowable on request — mmalex: *"panels get 256k, but the second 128k is … used-by-system-during-loads … hidden behind a 'get me the other 128k plz'"* (`get_panel_shadow_state`). Check the generation number; system may reclaim it during loads. |
| core0 temp scratch | 4 KB | `temp_alloc` / `make_temp_object<T>` (strict LIFO, brief) |
| PSRAM scratch | **~8 MB** | `get_psram_ptr()` / `get_psram_size()` |

**Rules:**
- **No heap.** No `new`/`malloc`/`free`/growing containers. Declare worst-case state as members.
- **No `#include`.** Headers are auto-injected before your code.
- ⭐ **Stack variables must stay under ~200 bytes** (mmalex). Big temporaries → members, PSRAM, or `temp_alloc`.
- **`static const` data lives in flash (rodata), not the 128 KB arena.** Read-only tables (window functions, coefficients, wavetables) are effectively free of the RAM budget. Only *mutable* state counts against the 128/256 KB.

**Practical placement rule (for DSP-heavy panels):** hot + small + random-access → **SRAM arena** (128/256 KB); big + sequential → **PSRAM**; read-only → **flash**.

**PSRAM specifics** (⭐ mmalex):
- Use it however you like; `get_psram_ptr()` = start, `get_psram_size()` = usable bytes.
- **It is slow.** It *loves sequential reads* and *hates scattered reads/writes* — read/write **linearly, in order**, wherever possible.
- The **last 128 KB** is the system FX delay buffer. `get_psram_size()` already subtracts it.
- You can use the **whole 8 MB** *if you don't use the system's `do_fx` in your DSP*. Otherwise everything except that last 128 KB is yours; the system touches nothing else.
- ⭐ **Reliability (mmalex):** RP2350+PSRAM has known glitch-under-heavy-load reports in the wild (the SparkFun-popularised init runs it *out of datasheet spec*; mmalex filed a bug). Plinky runs it **in-spec** and every unit gets a PSRAM RAM-check, "but… idk why, I don't treat it as trustworthy as normal RAM lol. it's certainly an order of magnitude slower." **Treat PSRAM as slow, sequential-friendly, belt-and-suspenders storage — not a drop-in for SRAM.**

---

## 4. Display & input

- Draw with `set_led(x, y, col)`; `leds_clear()` to blank. `(0,0)` top-left, y increases downward.
- Color: `LED_RGB(r, g, b)` with **5-bit channels (0–31)**.
  `#define LED_RGB(r,g,b) (uint32_t)(((g)<<24)|((r)<<16)|((b)<<8))`.
  Named colors: `RED=LED_RGB(31,0,0)`, `GREEN=(0,31,0)`, `WHITE=(15,15,15)`, `BLACK=0`, plus a `RAINBOW0..15` ramp. Map 8-bit sources with `>>3`.
- Pads: `get_touch_pressure_xy(x,y)`, drag origin via `get_touch_origin_x/y(x,y)`.
- Accelerometer: `get_accel_q24(axis, ...)`, `ACCEL_Q24_ONE_G` (see `ball.cpp`).
- The grid is one window onto a taller logical surface (pages below page 0, panel-settings pages above). Advertise with `get_num_pages()` / `get_num_panel_settings_pages()`.
- Higher-level widgets: `button`, `slider_t`, `xy_pad_t`, `knob2x2_t`, `radio_buttons`, `file_picker_t`, plus `leds_backup` / `leds_draw_transition_from_backup` for transitions.

---

## 5. Storage & file I/O

- ⭐ **ELM-Chan FatFs is available** (mmalex: *"there's not a general fatfs api but there is! elmchan fatfs is almost certainly included before your code and you can just use it. i forgot to document this."*). So `f_open` / `f_read` / `f_lseek` / `FIL` work from panel code → you can read arbitrary files (e.g. large binary assets) from the SD card.
- `read_rgb_ppm(filename, w, h, dest)` — loads an 8-bit binary RGB PPM (`P6`) from SD.
- Durable state: `on_serialise(...)` (song-like state) and `on_serialise_settings(...)` (prefs) via the JSON serialiser in `save_and_load.h`; loaded/saved through the panel loader. `serialise_psram_binary_footer(...)` appends large PSRAM ranges to the save file without base64 overhead.
- ⭐ mmalex recommends leaning on the system's `on_serialise` + built-in **song-slot save/load UI** — if only because it "presents as familiar to other Plinky users." The save system recently grew a **fast lossy codec** (`PSRAM_BINARY_FOOTER_CODEC_LOSSY_STEREO_8BIT`) that dumps PSRAM to SD alongside a save in ~1s; `worm` demonstrates it. Note it "abuses things slightly": `on_serialise` fields are JSON, but the PSRAM dumper appends a **raw binary tail after the JSON** (deliberately — base64 would be a 5:4 size increase).
- Getting files onto the device: eject the physical SD, copy, reinsert (trivial).

---

## 6. Build / IDE constraints

- Panels are authored in a **hosted web IDE**: one `.cpp`, `#include` banned, compiled as **C++**.
- ⭐ **Long source files are fine.** `blocks` (the flagship) is **one ~10k-line file**; the `Chords` panel is ~5,700 lines. There is an **arbitrary server-side source-size limit** (mmalex set it; raisable). mmalex on big ports: *"large codebases suck, don't do it 🙂 (tho i know you will)."*

---

## 7. Emulator / dev workflow

- There are **web and emulator builds** (guarded by `WASM` / `EMU`), with an **emulated 8 MiB PSRAM**.
- ⭐ **The emulator simulates an SD card** — it even ships an image of some smaller real-SD presets. **Reads work; writes may currently fail** (a read-only issue mmalex is fixing). Fine for read-only assets; savegame-style writes are not reliable yet.
- Practical loop: prototype heavy/algorithmic work as a **native desktop program** first (full speed, real files, no hardware), then port the thin Plinky glue into a panel and validate in the emulator, then on hardware.

---

## 8. Example panels (in `llm.txt`)

`totally_blank` (minimal skeleton) · `paint` (touch/drag drawing) · `ball`
(accelerometer + per-pixel render) · `knobs` (MIDI CC surface) · `looper` /
`worm` (PSRAM audio: 8-track looper, granular sampler) · `grid` (USB serial +
settings page) · `mics` (DSP analysis/FFT display).

---

## 9. Performance findings (DSP-heavy panels)

Learned building the Ambiotica ambient engine (a full effects chain in the ~2 ms
core1 block). Reusable for any DSP-heavy panel:

- **PSRAM is reached through a tiny (~8 KB) XIP cache.** Section 3 already notes PSRAM
  loves sequential and hates scattered access — the sharper truth is that even a *few
  concurrent scattered read streams* (e.g. several granular grains each reading a random
  spot in a big buffer, plus a write head) evict each other, so *every* access re-misses.
  A stage can be arithmetically trivial and still cost 100+ µs purely in miss latency.
- **The desktop can't see this.** A native harness (fast DDR, no XIP cache) is great for
  correctness and for finding arithmetic hotspots, but its render time is **not predictive
  of on-device CPU**. Profile the real budget on hardware (`time_us()` around each stage;
  watch `dsp MAX` and dropped blocks).
- **Levers that worked, all measured on-device:** store big audio buffers as **interleaved
  int16** (halves bytes and puts L+R in one cache line); minimize **concurrent scattered
  read streams** (fewer overlapping grains — 50 % Hann overlap is constant-amplitude, so it
  drops a stream for free); **half-rate** slow textures (reverbs, wash clouds); use a
  **window LUT** instead of per-sample trig; and **throttle coefficient re-pushes** during
  slow macro ramps (recomputing ~18 setters every 2 ms block is invisible to per-stage
  timers but spikes the total).
- Reliability: a smeared wash hides a lot — if a scattered-read stage is inaudible under
  the reverb, cutting it is free CPU.

---

## 10. Open questions

- Exact server-side source-size limit (and whether/how it's raised on request).
- Emulator SD **write** support timeline (mmalex "will fix").
- Whether the panel toolchain can link more than one translation unit (assume **no** — single `.cpp`).
