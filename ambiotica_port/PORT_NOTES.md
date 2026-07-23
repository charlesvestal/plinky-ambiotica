# Ambiotica → Plinky port notes

Private working area. Vendored proprietary DSP in `dsp/` (from
`schwung-parent/ambiotica-plugin`). Harness in `harness/`.

## STATUS: full chain runs clean on device ✅ (2026-07-23)

The complete 7-module Ambiotica chain (looper · granular · micro-loop · reverb ·
Spectra resonators · drift · bloom) runs on the Plinky via the core1 `on_dsp`
API, driving the built-in synth. Shipping build = `panel/plinky_ambiotica.cpp`
(regenerate via `panel/amalgamate.sh`; flash on `stage.plinky12.com`).

### The whole saga was ONE root cause: the core1 2 ms DSP budget
A 64-sample block @32 kHz = **2000 µs** on core1. Overrunning it crackles (sounds
like a "sample/bitrate mismatch"). Every symptom — fizz on 1 note, "clipping",
worse with polyphony, USB+analog both — was this. The device debug `printf`
(`dsp MAX/AVG` line + per-stage HF probe, `AMB_DEBUG`) was what finally showed it.

### Cuts that got us under budget (measured on device)
1. **Reverb → fast SRAM** (was 8-comb 150 KB in PSRAM; scattered PSRAM access was
   the original core0 killer). Now 4 combs (3 at L4+), ~62–79 KB in the arena.
2. **Fast-math trig** (`dsp/fast_math.h`): software `sinf/cosf/tanhf` on the M33
   cost hundreds of cycles; per-sample calls → polynomial approximations.
3. **Param memoize**: `fc_push_params` ran every audio block recomputing a
   *constant* chord (3× `powf` + `expf`); now skipped unless a slider moves.
4. **4-voice polyphony**: the built-in synth renders inside the *same* core1
   budget; 8 voices' render time was the playing-time overrun driver. 4 leaves
   headroom for the full FX.
5. Granular grains 12→4; harmony 6→3 voices (also for the 128 KB arena).

Device timing at the fix: **idle dsp ~1522/1916 µs, playing ~1800 µs** (under 2000
with margin). Overrun crackle gone.

### Known caveats / follow-ups
- 4-voice max (5th note steals oldest) — fine for pads; the tradeoff is the budget.
- Reverb 3–4 comb / harmony 3-voice (vs plugin 8/6): slightly less dense, character intact.
- Spectra chord fixed to C-minor; Gravity / Key-Chord / Event-Horizon / Dilate not yet exposed.
- v1.1 TODO: route the synth's reverb/delay SENDS into the chain (currently dry-only).
- `AMB_DEBUG` / `AMB_BYPASS` diagnostic builds documented in `panel/panel.cpp`.

## Milestone 1 — desktop fixed-arena harness ✅

`harness/` compiles the vendored DSP with `calloc/malloc/free` redirected to an
instrumented bump allocator, runs the validated chain wiring
(`harness/full_chain.h`: looper→granular shimmer-crossfade ∥ microloop → reverb,
mix-smoothed) at **32 kHz** in blocks, feeds four decaying-sine plucks, and
writes `out.wav` + a memory map.

**Result:** zero heap in the process path; correct ambient output (plucks build,
long wash decays after input stops; peak 0.32, no clipping/NaN).

### Memory map @ 32 kHz — FULL chain, int16 loop bed (measured)

| Module | Size | Access | Target |
|---|---|---|---|
| looper (int16) | 4.0 MB | sequential | PSRAM |
| microloop | 1.5 MB | semi-random | PSRAM |
| granular | 1.25 MB | scattered (**perf risk**) | PSRAM |
| reverb | 0.15 MB | scattered | **SRAM arena** (dodges PSRAM slowness) |
| harmony (Spectra) | 27 KB | delay lines | SRAM arena |
| drift (Flux) | 6 KB | small | SRAM |
| bloom | 0.1 KB | tiny | SRAM |
| **total** | **6.78 MB** | | **fits ~7.9 MB PSRAM ✅ (~1 MB headroom)** |

Full 7-module chain (`harness/full_chain.h`, all modules incl. Spectra/Drift/
Bloom + drift regeneration feedback) runs at 32 kHz, zero heap, **stable** (0
NaN, 0 clip under feedback), with the signature self-evolving tail. The extra 4
modules add only +34 KB. Harness chain is faithful in signal order + key makeups,
not a byte-exact `processBlock` clone (mutes/gravity/exact makeups + Spectra
prominence = later on-device fidelity tuning).

### Placement plan for Plinky
- **reverb → SRAM arena** (128/256 KB): small + scattered = ideal fit.
- **looper → PSRAM**, sequential (PSRAM-friendly), *but must be capped* (see below).
- **granular, microloop → PSRAM**: granular's scattered grain reads are the main
  on-device performance unknown (PSRAM is ~10× slower, hates scatter).
- read-only tables → flash.

### The loop-length problem — RESOLVED (int16 loop bed) ✅
`kLoopBufMaxSeconds = 32` (8 bars @ 60 BPM) → 8 MB float looper, over budget.
**Fix shipped in the harness:** `looper.c` `buf_L/buf_R` are now `lsamp_t`,
`float` by default or **`int16` under `-DLOOPER_I16`** (via `ld()`/`st()`
convert-on-read/write; float build is behaviourally identical).

Result (measured, `harness/`):
- looper 8 MB → **4 MB**; chain total 10.65 MB → **6.74 MB → fits ~7.9 MB PSRAM**.
- **Full 32 s loop length kept.**
- Audio A/B (`out_float.wav` vs `out_i16.wav`): **76.8 dB SNR, −80 dB peak diff → inaudible.**
- int16 is Plinky-native (`get_psram_ptr()` is `int16_t*`), sequential-access
  (no scatter penalty), and halves bytes/sample across the slow PSRAM bus.

Build both: `harness/build.sh` → `amb_harness_float`, `amb_harness_i16`.

## Milestone 2 — panel scaffold (`panel/`)

Single-file Plinky panel, blind-coded against `llm.txt` (no local SDK to compile
against — validated in the emulator by the user).

- `panel/alloc_prelude.h` — two-region arena allocator; `#define calloc` →
  PSRAM (`get_psram_ptr`) for looper/granular/microloop/reverb, or the panel's
  64 KB SRAM pool for harmony/drift/bloom + structs. reverb (150 KB, scattered)
  is forced into PSRAM → **#1 on-device perf risk to measure**.
- `panel/panel.cpp` — `ambiotica_panel : panel_t`: play surface (left 8 cols) →
  `play_synth` (built-in synth); FX macro sliders (right 8 cols) → `full_params`;
  **Ambiotica inserted in `on_dsp_final_mix`** on the synth's dry stereo (Plinky
  `do_fx` left unused — Ambiotica is the FX); `fc_render_block` per 64-frame
  block; `fc_state` + scratch as members (nothing large on the stack).
- `panel/amalgamate.sh` → `plinky_ambiotica.cpp` (**2,513 lines**, well under the
  ~10k IDE limit). DSP half validated: compiles + links + runs as one TU
  (no dup statics, calloc redirect works).

### VERIFY in emulator (blind-coded — see panel.cpp header for the full list)
sample rate; `on_dsp_final_mix` scale/hook; `play_surface_t`/`do_play_surface`/
`play_synth`; synth preset; `slider_t`/`last_widget_new_value`; `palette`/
`DIMMEST`/`BLOCK_SIZE`; `on_serialise` field macros; libm availability;
reverb-in-PSRAM CPU cost.

## Open risks / TODO
- PSRAM scattered-access performance for granular (only measurable on-device).
- Real RP2350 CPU headroom (desktop 239× realtime is not predictive).
- Chain currently omits bloom / drift / harmony(Spectra) / Gravity — add after core fits.
- Process scratch buffers in `harness/full_chain.h` are ~14 KB of stack; on Plinky
  (stack < ~200 B) these must become panel members / arena.
