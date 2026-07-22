# Ambiotica → Plinky port notes

Private working area. Vendored proprietary DSP in `dsp/` (from
`schwung-parent/ambiotica-plugin`). Harness in `harness/`.

## Milestone 1 — desktop fixed-arena harness ✅

`harness/` compiles the vendored DSP with `calloc/malloc/free` redirected to an
instrumented bump allocator, runs the validated chain wiring
(`ambiotica_chain.h`: looper→granular shimmer-crossfade ∥ microloop → reverb,
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

## Open risks / TODO
- PSRAM scattered-access performance for granular (only measurable on-device).
- Real RP2350 CPU headroom (desktop 239× realtime is not predictive).
- Chain currently omits bloom / drift / harmony(Spectra) / Gravity — add after core fits.
- Process scratch buffers in `ambiotica_chain.h` are ~14 KB of stack; on Plinky
  (stack < ~200 B) these must become panel members / arena.
