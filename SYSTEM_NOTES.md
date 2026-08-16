# Plinky 12 - System Notes for Panel Development

Working reference for building **panels** on the Plinky 12. Compiled from the
public panel API dump (`llm.txt`) and direct answers from the Plinky devs
(**mmalex**, **makingsoundmachines**) on Discord, 2026-07-22 - several of which
are **undocumented** and marked ⭐.

---

## 1. Hardware

| | |
|---|---|
| MCU | **RP2350** (dual ARM Cortex-M33), the Raspberry Pi Pico 2 chip |
| PSRAM | **8 MB** external QSPI (shared scratch for panels) |
| Display | **16×16 RGB LED grid** - this is the *entire* screen |
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
| `on_dsp(const int16_t*, int16_t*, mix_buffers_t*)` | **core1** | per 64-sample block | audio only. Return `false` to run the standard FX chain on `mix_buffers_out`, `true` if you wrote final stereo yourself. |
| `on_click / on_touch / on_midi` | core0 | event | side buttons / raw pad edges / incoming MIDI |
| `setup_default_panel_state()` | core0 | once | build default song/panel state after settings load |
| `on_serialise / on_serialise_settings` | core0 | save/load | durable state / durable prefs (JSON) |

⭐ **Compute budget (mmalex):** *"go for your life on core0 - it only blocks UI."*
Heavy sustained work in `on_ui` is fine; it just stalls UI refresh, **not audio**
(audio is core1). `on_sequence` is core0 but IRQ context.
⭐ **core1 is DSP only.** You cannot run game/render logic there.

---

## 3. Memory model

| Region | Size | Notes |
|---|---|---|
| Panel object arena | **128 KB** | your whole `panel_t` subclass instance + members (mutable state) live here. Always yours. |
| Second 128 KB (shadow) | 128 KB | ⭐ borrowable on request - mmalex: *"panels get 256k, but the second 128k is … used-by-system-during-loads … hidden behind a 'get me the other 128k plz'"* (`get_panel_shadow_state`). Check the generation number; system may reclaim it during loads. |
| core0 temp scratch | 4 KB | `temp_alloc` / `make_temp_object<T>` (strict LIFO, brief) |
| PSRAM scratch | **~8 MB** | `get_psram_ptr()` / `get_psram_size()` |

**Rules:**
- **No heap.** No `new`/`malloc`/`free`/growing containers. Declare worst-case state as members.
- **No `#include`.** Headers are auto-injected before your code.
- ⭐ **Keep stack objects under ~128 bytes; treat 256 bytes as the hard upper bound** (the
  published figures). Big temporaries → members, PSRAM, or `temp_alloc`.
- **`static const` data lives in flash (rodata), not the 128 KB arena.** Read-only tables (window functions, coefficients, wavetables) are effectively free of the RAM budget. Only *mutable* state counts against the 128/256 KB.

**Practical placement rule (for DSP-heavy panels):** hot + small + random-access → **SRAM arena** (128/256 KB); big + sequential → **PSRAM**; read-only → **flash**.

**PSRAM specifics** (⭐ mmalex):
- Use it however you like; `get_psram_ptr()` = start, `get_psram_size()` = usable bytes.
- **It is slow.** It *loves sequential reads* and *hates scattered reads/writes* - read/write **linearly, in order**, wherever possible.
- The **last 128 KB** is the system FX delay buffer. `get_psram_size()` already subtracts it.
- You can use the **whole 8 MB** *if you don't use the system's `do_fx` in your DSP*. Otherwise everything except that last 128 KB is yours; the system touches nothing else.
- ⭐ **Reliability (mmalex):** RP2350+PSRAM has known glitch-under-heavy-load reports in the wild (the SparkFun-popularised init runs it *out of datasheet spec*; mmalex filed a bug). Plinky runs it **in-spec** and every unit gets a PSRAM RAM-check, "but… idk why, I don't treat it as trustworthy as normal RAM lol. it's certainly an order of magnitude slower." **Treat PSRAM as slow, sequential-friendly, belt-and-suspenders storage - not a drop-in for SRAM.**

---

## 4. Display & input

- Draw with `set_led(x, y, col)`; `leds_clear()` to blank. `(0,0)` top-left, y increases downward.
- Color: `LED_RGB(r, g, b)` with **5-bit channels (0–31)**.
  `#define LED_RGB(r,g,b) (uint32_t)(((g)<<24)|((r)<<16)|((b)<<8))`.
  Named colors: `RED=LED_RGB(31,0,0)`, `GREEN=(0,31,0)`, `WHITE=(15,15,15)`, `BLACK=0`, plus a `RAINBOW0..15` ramp. Map 8-bit sources with `>>3`.
- Accelerometer: `get_accel_q24(axis, ...)`, `ACCEL_Q24_ONE_G` (see `ball.cpp`).
- Higher-level widgets: `button`, `shift_button`, `invisible_button`, `slider_t`, `xy_pad_t`, `knob2x2_t`, `radio_buttons`, `file_picker_t`, plus `leds_backup` / `leds_draw_transition_from_backup` for transitions.

### ⭐ NEVER COMPARE A RAW TOUCH VALUE

Direct from the Plinky side (2026-08-07): *"you should never have to compare a raw value - use
button, shift_button, slider, xy_pad, play_surface if you need one of those."* `llm.txt` explains
why: touch is *"mostly pad-by-pad and stateless, but fingers can slide across the grid."* The
widget layer is where slide handling and press/release state live. Read pads raw and a momentary
pressure dip reads as a release, and pressure sliding in from a neighbour reads as a press.

`plinky-ambiotica` learned this the expensive way. A spuriously-released shift key turned every
paint stroke into a silent erase, and users reported dropped taps and dead modifiers for weeks.

- **A held modifier is `shift_button`** - "the same as `button(...)`, except it returns true for
  as long as the pad is held". `invisible_button` is the same touch behaviour without drawing,
  for when you want to paint the LED yourself.
- **Edges come from `is_last_widget_pressed()` / `_released()` / `_held()`**, which refer to the
  most recently emitted widget - so read them immediately after the call, with nothing between.
- **A widget may only be emitted ONCE per frame.** Emitting twice draws twice and stomps the
  last-widget state. If several places need a modifier, emit it once into a member and have
  everything read that.
- **Immediate mode is ORDER-DEPENDENT.** Emit modifiers BEFORE anything that tests them. Raw
  reads were order-free; widgets are not.
- **Modifiers must be `NOT_ISOLATED`** (inferred from `ISOLATED` "rejects taps that are crowded
by neighbouring touches" - reasonable, but not stated as a rule anywhere). There is also a
documented API for exactly this: `set_touch_origin_ignore_row_mask(...)` marks pads as
modifier-style so they "should not donate their origin to neighbouring newly-pressed pads",
and is meant to be called near the top of `on_ui(...)` for shift/record/clear pads. `ISOLATED` *"rejects taps that are crowded by
  neighbouring touches"*, and a modifier exists to be held WHILE another pad is tapped, so
  crowding IS the gesture. Conversely, keep standalone nav pads `ISOLATED` - it is what stops a
  dense corner of adjacent pads triggering each other.
- Origin tracking is **explicit**, not automatic: `touch_originates_inside_region(padx, pady, x1,
  y1, w, h)` takes the rect directly. Buttons do NOT reject slid-in pressure on their own.

### ⭐ PANEL PAGES LIVE AT y = N*16, AND set_led/button TAKE ABSOLUTE y

The grid is one 16x16 window onto a taller logical surface. Page 0 is at logical `y = 0`,
page 1 at `y = 16`, page 2 at `y = 32`. Panel **settings** pages go the other way: page -1 is
at `y = -16`, page -2 at `y = -32`. Override `get_num_pages()` to advertise the extra pages.

`set_led(x, y, ...)` and `button(x, y, ...)` take the **absolute logical y**, not a
page-relative one. So a second page must be drawn at `y = 16 + row`, and the whole-page
helpers take a `y` argument for exactly this: both stock panels that add a save/load page call
`panel_page.saveload(16, ...)`.

Drawing a second page at rows 0..15 puts it a whole page above the viewport: nothing renders
and none of its buttons can be pressed. This cost a silently dead scene picker in plinky-life,
because an earlier revision of this file asserted the opposite - "draw every page at absolute
y 0..15, do not build your own pages that way". That was wrong.

### Brightness: the dim tiers are far dimmer than they look

`WHITE` is `LED_RGB(15,15,15)` - already **half scale** - so the macros land differently on it
than on a full-31 colour. `DIMMESTEST` is `>>3`, giving **1 of 31 on white**: the dimmest thing
the hardware can show, and effectively invisible. A user reported muted tracks as blank because
of it. `DIMMEST(WHITE)` is 3; `fade_col(WHITE, 48)` gives 2 if you want the step between.

### The mounted faceplate IS readable (masking is a choice, not a requirement)

The firmware reads the overlay off resistor straps. It is spelled **`frontpanel`**, not
faceplate/overlay/panel_art - searching for the latter finds nothing and invites the wrong
conclusion that no such API exists. `grid.cpp` uses it to shift its LEDs down two rows on
Chords and Drums.

```c
int get_frontpanel_code(void);        // FRONT_PANEL_CODE_NONE/BLOCKS 0x0, TOADSTEP 0x2,
                                      // DRUMS 0x10, CHORDS 0x20
int get_frontpanel_orientation(void); // 0 = missing, 1 = normal, 2 = upside down
```

⭐ **Read it in a MEMBER INITIALISER, at construction.** This is the whole trick, and it is
why `grid.cpp` does `int8_t y_offset = (get_frontpanel_code() == ...) ? 2 : 0;` as a member
default rather than calling it from a hook.

Confirmed on a Chords unit: the call returns the plate at construction and **`0` from
`on_ui` later**, while `get_frontpanel_orientation()` reports `1` throughout - so the
firmware knows a panel is mounted but the code is only readable early. Calling it per frame
looks tidier, compiles, logs a plausible `0`, and silently never detects anything. Cost most
of a session.

⭐ **Ship a manual override anyway.** `grid.cpp` persists `y_offset` as a setting with its
own page for exactly this reason: detection is the default, not the contract.

Chords and Drums print the **same** synth page (checked against `panel_art/chords.png` and
`panel_art/drums.png`, label for label) and both give pad circles only on rows 2..13. Blocks
is a bare unlabelled 16x16. Toadstep's printed fader order is exactly `preset_pages_t::edit()`'s
column order, so the stock layout is already correct there.

⭐ **The stock save/load wrappers cannot be placed; their pieces can.**
`preset_pages_t::saveload_action(idx, y)` and `panel_page_t::saveload(y)` hardcode their
cancel and OK to `(14, y+15)` and `(15, y+15)`, which on Chords is the transport corner. Do
not conclude the picker is immovable - call the parts instead:
`picker.preset_picker(idx, y1, y2)`, `preset_save_button(idx, x, y)`,
`preset_load_button(idx, x, y)`, and `panel_picker(y1, y2)` / `panel_save_button` /
`panel_load_button`. `plinky-ambiotica` builds both pages this way. Note
`panel_load_button` only STAGES a load: poll `is_panel_load_staged()` and then
`request_panel_load_finalise()`, and return immediately if it accepts.

Chords prints **SAVE at (12,14) and LOAD at (13,14)**, and row 15 is `● ✕ ■ ▶` at x12..15.
Row 0 is `ALL / TREBLE / BASS / MELODY`, row 1 is `RHYTHM LENGTH VELOCITY CHORD ARP KEY
TREBLE MID BASS MELODY XY MODULO PROB PATTERN FILL UNLOCK`.

Panel art: `https://plinky12.com/panel_art/<name>.png`, basic auth `p12code` / `jollygood`.

### ⭐ `do_play_surface` derives pitch from BOTH axes, which runs away over many strings

`do_play_surface` takes pitch from the string root AND the position **along** the string. The
per-string interval is a caller parameter (`fill_scale_string_roots(..., interval_degrees)`)
with no documented default, so "a fourth per string" is a convention, not an API fact. Use
the same offset across a full-height 16-row grid and the multiplication runs away: measured
**7.4 octaves** on a Chords faceplate (12 usable rows) and 8.6 on a blank one, top corner clamping
flat at MIDI 127, against a sequencer covering 3.2.

It is not a bug in `do_play_surface`. The row count is the variable, and a panel using rows as
strings has 12 to 15 of them rather than 8.

**Any per-row offset above +1 degree overshoots a full-height grid.** Fourths (+3/row) give 7.4
octaves; +2/row gives 4.8. Only +1 lands near the instrument's own range - 2.4 octaves on Chords,
3.0 on a blank plate in a pentatonic scale.

**How to apply:** for a full-height surface, skip `do_play_surface` and drive
`update_from_touch` / `update_voices` / `get_finger_for_voice` yourself, with `STRINGOPHONIC_MONO`
so a slide along a row holds its note rather than retriggering. Then map pitch from the row alone.
Anchor the bottom row at scale degree 0: a window derived from world coordinates lands wherever
the arithmetic drops it and will silently exclude the tonic.

### ⭐ Enabling the mic switches OFF four LEDs

`codec_enable_mic(true)` makes the firmware disable **the 2 LEDs beside each microphone hole**
(they inject noise into the recording). That is pads **(0,1), (0,2), (15,1), (15,2)** - the top
corners of the pad area. The behaviour is documented ("the 2 leds adjacent to each microphone hole are disabled");
the specific pad coordinates below are inferred, not published, and it cost a full debugging
session: a user reported them as dead LEDs, and two other people could not reproduce it because
their input source was `off`. **Check `audio_source` before investigating dark corner pads.**

---

## 5. Storage & file I/O

- ⭐ **ELM-Chan FatFs is available** (mmalex: *"there's not a general fatfs api but there is! elmchan fatfs is almost certainly included before your code and you can just use it. i forgot to document this."*). So `f_open` / `f_read` / `f_lseek` / `FIL` work from panel code → you can read arbitrary files (e.g. large binary assets) from the SD card.
- `read_rgb_ppm(filename, w, h, dest)` - loads an 8-bit binary RGB PPM (`P6`) from SD.
- Durable state: `on_serialise(...)` (song-like state) and `on_serialise_settings(...)` (prefs) via the JSON serialiser in `save_and_load.h`; loaded/saved through the panel loader. `serialise_psram_binary_footer(...)` appends large PSRAM ranges to the save file without base64 overhead.
- ⭐ mmalex recommends leaning on the system's `on_serialise` + built-in **song-slot save/load UI** - if only because it "presents as familiar to other Plinky users." The save system recently grew a **fast lossy codec** (`PSRAM_BINARY_FOOTER_CODEC_LOSSY_STEREO_8BIT`) that dumps PSRAM to SD alongside a save in ~1s; `worm` demonstrates it. Note it "abuses things slightly": `on_serialise` fields are JSON, but the PSRAM dumper appends a **raw binary tail after the JSON** (deliberately - base64 would be a 5:4 size increase).
- Getting files onto the device: eject the physical SD, copy, reinsert (trivial).

---

## 6. Build / IDE constraints

- Panels are authored in a **hosted web IDE**: one `.cpp`, `#include` banned, compiled as **C++**.
- ⭐ **Long source files are fine.** `blocks` (the flagship) is **one ~10k-line file**; the `Chords` panel is ~5,700 lines. There is an **arbitrary server-side source-size limit** (mmalex set it; raisable). mmalex on big ports: *"large codebases suck, don't do it 🙂 (tho i know you will)."*

---

## 6b. Gotchas that cost real debugging time

- ⭐ **`printf` / Device Logs is BROKEN on the STAGING IDE** (`stage.plinky12.com`). The panel
  compiles and runs, output never appears. **Flash anything you need logs from via
  `plinky12.com`.** Cost a long "why is nothing printing" detour.
- ⭐ **Loading a panel kills the WebUSB log.** Firmware bug, reproducible on stock panels, the
  instrument is unaffected. Do not re-debug it as a panel hang.
- **A panel's SD folder is created by SAVING**, not provisioned. An empty picker is an empty
  state, not a broken feature. Also: the picker's name buffer is `char[17]`, and a panel name
  over 16 chars silently kills the folder scan.
- **`setup_default_panel_state()` does NOT run on a staged load** (which is how the IDE loads a
  panel). Rebuild allocations in `on_load_finished()` or things silently die.
- **`on_serialise` named fields are only written back when PRESENT**, so any field a save omits
  keeps whatever is already in the object - i.e. **the previously loaded scene's value**. This
  is not a back-compat issue: two saves from the same build differ if one never set a field.
  **Reset every deserialised field before `OBJECT_BEGIN`.**
- **`reverbbuf` is a member of `mix_buffers_t`**: `int16_t reverbbuf[REVERBBUF_SAMPLES]`, and
  the docs themselves write `mix_buffers.reverbbuf`. It is 64 KB (`REVERBBUF_SAMPLES` is 32768,
  mono) of fast RAM, usable when `on_dsp` returns true but **only within a block** - putting
  anything long-lived there froze the instrument on the first preset load. (An earlier revision
  of this file called it a macro that must be used bare. That was wrong.)
- **The desktop cannot measure CPU.** The RP2350 reaches big buffers through PSRAM behind a tiny
  XIP cache; things that are free on a desktop cost 150 µs on device. Profile on hardware, and
  change one thing at a time. Note the profile build's own `printf` inflates the UI timings.

---

## 6c. Submitting to the community library

`plinkysynth/community-panels`, layout `author/panel/panel.cpp` (+ optional `README.md`,
`artwork.png`). Metadata is a block comment that must be the **first** thing in the file.

- **`@Author` and `@Firmware` are REQUIRED.** `@Firmware` is `latest`, a channel (`beta` /
  `alpha` / `release`), or a 4-char build code. **The maintainer owns this value** - they can
  only bless artifacts for firmware in Plinky's `versions.json`.
- Other fields: `@Name`, `@Description`, `@Preferred Panels`, `@Tags`, `@Documentation`,
  `@Video`, `@Discussion`, `@Category`. `@Artwork` is banned (ship `artwork.png` beside it).
- ⭐ **The maintainer EDITS YOUR FILE BY HAND after you submit.** On `ambiotica` they did it
  three times (`@Video`, then `@Firmware` twice). If your `.cpp` is generated, every one of
  those is silently reverted by your next build. **After any merge, diff the merged file
  against your generated one and copy back what they changed.**

---

## 7. Emulator / dev workflow

- There are **web and emulator builds** (guarded by `WASM` / `EMU`), with an **emulated 8 MiB PSRAM**.
- ⭐ **The emulator simulates an SD card** - it even ships an image of some smaller real-SD presets. **Reads work; writes may currently fail** (a read-only issue mmalex is fixing). Fine for read-only assets; savegame-style writes are not reliable yet.
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
  loves sequential and hates scattered access - the sharper truth is that even a *few
  concurrent scattered read streams* (e.g. several granular grains each reading a random
  spot in a big buffer, plus a write head) evict each other, so *every* access re-misses.
  A stage can be arithmetically trivial and still cost 100+ µs purely in miss latency.
- **The desktop can't see this.** A native harness (fast DDR, no XIP cache) is great for
  correctness and for finding arithmetic hotspots, but its render time is **not predictive
  of on-device CPU**. Profile the real budget on hardware (`time_us()` around each stage;
  watch `dsp MAX` and dropped blocks).
- **Levers that worked, all measured on-device:** store big audio buffers as **interleaved
  int16** (halves bytes and puts L+R in one cache line); minimize **concurrent scattered
  read streams** (fewer overlapping grains - 50 % Hann overlap is constant-amplitude, so it
  drops a stream for free); **half-rate** slow textures (reverbs, wash clouds); use a
  **window LUT** instead of per-sample trig; and **throttle coefficient re-pushes** during
  slow macro ramps (recomputing ~18 setters every 2 ms block is invisible to per-stage
  timers but spikes the total).
- Reliability: a smeared wash hides a lot - if a scattered-read stage is inaudible under
  the reverb, cutting it is free CPU.

---

## 10. Open questions

- Exact server-side source-size limit (and whether/how it's raised on request).
- Emulator SD **write** support timeline (mmalex "will fix").
- Whether the panel toolchain can link more than one translation unit (assume **no** - single `.cpp`).
