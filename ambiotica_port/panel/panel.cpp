/* Ambiotica-on-Plinky — panel.
 *
 * Play surface (left half) drives the built-in synth; its dry bus feeds the Ambiotica
 * chain (full_chain.h), which runs on core1 in on_dsp() and owns the output (the
 * built-in FX are silenced — Ambiotica IS the FX). See PORT_NOTES.md for the
 * core1-budget story. Build config: amb_config.h (LOOPER_I16) concatenated by
 * amalgamate.sh; per-module flags live in their own files (shimmer/cloud off in
 * microloop.c). Reverb is always the Dattorro plate.
 *
 * Laid out for the CHORDS overlay: the play UI occupies rows UI_Y..UI_Y1 (the middle 12,
 * the only rows the overlay draws pads for) and the reserved rows carry control pads under
 * their printed labels — KEY / BANK set key and mode, SYNTH and PRESET jump to the stock
 * editor pages, SCALE comes back. No physical side button is claimed. See the two layout
 * enums below.
 */
#define PANEL_PAD_COLOR TEAL
#define AMB_SR 32000.0
#define FC_SOFT_CLIP            /* soft-limit the chain output (see full_chain.h) */
/* Attenuate the Plinky synth before the chain: its polyphonic bus sums much hotter
 * than the plugin's single host input, and the whole chain (loop x1.9 makeup, drift
 * regen, layer sums) is tuned around a ~0.18 peak input (see ambiotica-plugin
 * tools/buildup_test.cpp). 0.12 lands the raw poly bus near that nominal level. */
#define AMB_IN_GAIN 0.12f

/* Slider column order (col 8..14). The two loopers sit together (Orbit = main loop,
   Satellite = micro-loop) with the granular scatter after them — grouped for legibility
   rather than strict signal-chain order (looper -> granular -> microloop). */
enum { FX_ORBIT, FX_SATELLITE, FX_CONSTELLATE, FX_TAIL, FX_FLUX, FX_SPECTRA, FX_MIX, FX_N };

/* Vertical layout. The panel is designed for the CHORDS grid, which claims the top two
   and bottom two rows for its own control rows, so ALL of our UI lives in the middle 12
   (rows 2..13) and the four reserved rows are left dark by leds_clear().
   Sliders simply shrink to UI_H. The play surface is NOT compressed — do_play_surface
   keeps one scale step per pad (string_pos = h-1-rely), so a shorter h just lifts each
   string's bottom (lowest) note up two rows and drops the top two notes. */
enum {
    UI_Y      = 2,                      /* first usable row */
    UI_H      = 12,                     /* usable height */
    UI_Y1     = UI_Y + UI_H - 1,        /* last usable row (13) */
    UI_MID_UP = UI_Y + UI_H / 2 - 1,    /* upper centre row (7)  — bipolar zero, upper half */
    UI_MID_DN = UI_Y + UI_H / 2,        /* lower centre row (8)  — bipolar zero, lower half */
    UI_HALF   = UI_H / 2 - 1,           /* rows of travel from centre to either end (5) */
};

/* Control pads. The Chords overlay is PRINTED on the hardware, so a pad keeps its label
   whatever page is showing — which is why the back button can sit at the same physical
   spot (SCALE) on all three pages. We light only the pads whose printed label matches
   what we actually do; every other reserved pad stays dark, including the row-15
   transport corner (we have no sequencer). No physical side button is claimed — they
   keep their stock BPM / page-cycle behaviour. */
enum {
    CTL_TOP    = 0,     /* top control row — the stock page strip (AUDIO IN..MIDI) */
    CTL_TOP2   = 1,     /* second control row — SEQUENCE / CONDITION / GENERATE labels */
    /* Row 1 carries the stock CONDITION and GENERATE groups, and both mean here what they
       mean on Chords. PROB is a condition — "a step is triggered or not, depending on a
       percentage chance". FILL is generate — "styles on how to fill the sequence" — which
       is exactly what a reroll does, so that is where randomise lives.
       NB Chords has no reroll pad; the (12,15) circle is RECORD, not a reroll. Blocks and
       Toadstep both have a dedicated reroll key, but their silkscreens are not ours. */
    /* "MODULO" (row 1, col 11) — the other half of CONDITION, and the reason the ratios are
       NOT crammed into PROB's ring: the manual gives each pad its own meaning, so putting
       "play the 2nd of every 4" anywhere but here would light one pad with another's job
       while leaving this one dark. */
    COL_MODULO = 11,
    COL_PROB   = 12,    /* "PROB" (row 1) — hold to see and edit per-step probability */
    COL_REROLL = 14,    /* "FILL" (row 1) — hold + tap a target to randomise it */
    /* "PATTERN" (row 1, col 13) — the other half of GENERATE, and FILL's opposite number:
       press a step to replace a track with a Euclidean rhythm of that many pulses, then
       slide to rotate it. FILL is random and builds up, PATTERN is ordered and replaces.
       Position verified against the physical faceplate — NOT against panel_art/chords.png,
       which is a stale revision (it prints SHUFFLE at row 1 col 9 where hardware prints
       MELODY, and 6/7/8/9 at row 0 cols 8-11 where hardware prints FWD/REV/RND/PING).
       Cols 11-15 are identical in both, so this run of pads is safe. */
    COL_PATTERN = 13,
    /* Column 15 rows 10-12 are printed MUTE / EDIT / ROOT — the side controls of the stock
       45-chord palette. On the PLAY page that column is our Gravity/Event Horizon slider,
       so they are only claimed on the drums page, where the column is genuinely free.
       Only MUTE is lit: EDIT would want per-track level/pan (we bake those per voice) and
       ROOT's obvious job, resetting transpose, is already x + KEY. A pad stays dark until
       it has a function that matches its label. */
    COL_MUTE   = 15,
    ROW_MUTE   = UI_Y + DRUM_TRACKS,   /* row 10, beside where the chord palette starts */
    CTL_UP     = 14,    /* bottom row A — the printed ▲ / upper-label row */
    CTL_DN     = 15,    /* bottom row B — the printed ▼ / lower-label row */
    COL_KEY    = 0,     /* "KEY"    ▲/▼ — key, around the circle of fifths */
    COL_BANK   = 6,     /* "BANK"   ▲/▼ — mode (Ionian/Aeolian/Dorian/Lydian/Mixolydian) */
    COL_SCALE  = 8,     /* "SCALE"  (row 14) — back to the play surface */
    COL_PRESET = 9,     /* "PRESET" (row 0)  — synth preset browser page */
    COL_SONG   = 10,    /* "SONG"   (row 15) — whole-scene (panel) save/load page */
    COL_SYNTH  = 11,    /* "SYNTH"  (row 15) — synth editor page */
    COL_TRACKS = 9,     /* "TRACKS" (row 14) — drum sequencer page */
    COL_SAVE   = 12,    /* "SAVE"   (row 14) — commit, on the picker pages only */
    COL_LOAD   = 13,    /* "LOAD"   (row 14) — commit, on the picker pages only */
    COL_X      = 13,    /* printed × (row 15) — the stock SHIFT key. The Chords manual:
                           "hold it and tap one of the other pads" to reset or delete. */
    COL_STOP   = 14,    /* printed ▢ (row 15) — the stock transport corner */
    COL_PLAY   = 15,    /* printed ▷ (row 15) */
};

/* Pages are a window onto a taller surface: page N lives at logical y = N*16. Every page
   places its content at +UI_Y so it lands on the printed pad circles and leaves the
   control rows free for the nav bar. */
enum { PAGE_PLAY = 0, PAGE_DRUMS = 1, PAGE_SYNTH = 2, PAGE_PRESET = 3, PAGE_SCENE = 4, PAGE_N };

/* Drum tracks own preset slots 1..8; slot 0 stays with the play surface, 9..11 spare.
   (MAX_SYNTH_PRESETS is 12, all resident at once.) */
#define DRUM_PRESET_BASE 1

/* Panel settings pages live ABOVE page 0 (right-up from the play surface), at y = -16*n. */
enum { SET_SOURCE = -1, SET_IN_LEVEL = -2, SET_N = 2 };

/* External input is attenuated like the synth bus is (see AMB_IN_GAIN): the chain is tuned
   around a ~0.18 peak input, and a line/mic feed can arrive at full scale. */
#define AMB_EXT_IN_GAIN 0.25f

/* NOTE: keep this name SHORT. The class name becomes the panel name (when no @Name is
 * set), the runtime prefixes it with "u_", and the file picker stores folder names in
 * char[17] — 16 chars plus NUL. "ambiotica_panel" made "u_ambiotica_panel", 17 chars,
 * one over: the folder scan truncated and failed forever, so no slot was ever
 * selectable, so the save that would have created /PLINKY/u_<panel>/ could never fire.
 * "u_ambiotica" is 11. It also matches the community-panels key. Diagnosed by mmalex. */
struct ambiotica : panel_t {
    looper_t* looper = 0; granular_t* granular = 0; microloop_t* microloop = 0;
    harmony_t* harmony = 0; bloom_t* bloom = 0; drift_t* drift = 0;
    drums_t*  drums = 0;
    bool dsp_ok = false;

    /* x0x sequencer. The PATTERN lives here rather than in drums_t so it serialises with the
       scene — drums_t is in PSRAM, which on_serialise never sees. Flat rather than [t][s] so
       it round-trips through one FIELD_BASE64. A byte per step is velocity, 0 = off. */
    unsigned char   pattern[DRUM_TRACKS * DRUM_STEPS] = {0};
    clock_divider_t drum_clock;
    int             drum_step = 0;
    /* MODULO is a SECOND condition per step, independent of probability — Chords prints both
       under CONDITION and its manual keeps them apart ("Modulo to have individual steps only
       play once out of N times, Probability to set a chance percentage"). So it gets its own
       byte per step rather than stealing range from the probability byte: 0 = none, else
       1+index into kConds. A scene saved before this existed simply has no "dmod" field, so
       the array stays zeroed and every old pattern plays exactly as it did. */
    unsigned char   pattern_mod[DRUM_TRACKS * DRUM_STEPS] = {0};
    unsigned int    drum_pass = 0;     /* completed loops of the pattern — what modulo counts */
    short           cond_sel = -1;     /* step being inspected by a PROB/MODULO hold, -1 = none */
    unsigned char   drum_paint = 0;    /* gesture mode: 0 idle, 1 painting on, 2 erasing, 3 modifier used */
    /* Live PATTERN (Euclid) gesture. Unlike every other modifier on this page it spans more
       than one frame — the press locks the pulse count, the drag rotates — so it needs to
       remember what it started on. eu_track < 0 means no gesture is running. */
    signed char     eu_track  = -1;    /* track the gesture owns; other rows are ignored */
    signed char     eu_anchor = 0;     /* column pressed — rotation is measured from here */
    signed char     eu_last   = -1;    /* last column visited, so we regenerate only on change */
    unsigned char   eu_k      = 0;     /* pulse count, locked at press */
    unsigned char   drum_mute = 0;     /* bit per track; muted tracks stop triggering */
    bool            mute_hit_track = false;  /* did this MUTE hold actually toggle anything? */
    bool            preset_report_done = false;   /* one-shot preset/kit dump (see report_presets) */
    int             drum_kit = -1;      /* -1 = the generated kit; else an index into kKits */
    int             drum_transpose = 0;   /* semitones, applied to the whole kit */
    /* Selection is deliberately separate from what is LOADED. Loading a kit runs the chunk
       planner, which blocks core0 for tens of ms — so scrolling a 20-entry ring must not
       load every entry it passes. BANK moves drum_kit_sel and arms a timer; only when the
       timer expires does the selection become real. */
    int             drum_kit_sel = -1;
    int             drum_kit_arm_us = 0;
    int             nav_cooldown_us = 0;  /* input ignored this long after a page change */

    full_params fx;      /* target macros (set from the sliders in on_ui) */
    full_params fx_sm;   /* per-block-smoothed macros actually fed to the chain (de-click) */
    fc_state    st;
    /* Dattorro reverb + harmony + drift + bloom live here (fast SRAM) — their
     * scattered delay-line access is too slow in PSRAM. Fits the 128 KB panel arena.
     * Looper/micro-loop/granular (big, sequential-ish) stay in PSRAM. */
    unsigned char sram_pool[88 * 1024];

    float sL[BLOCK_SIZE], sR[BLOCK_SIZE], oL[BLOCK_SIZE], oR[BLOCK_SIZE];
    play_surface_t play;
    slider_t       fxslider[FX_N];
    unsigned char  fx_val[FX_N];
    /* Held for its parts, not its pages: slider_banks feed the synth page's two slider
       blocks (24 of the 32), the_xy_pad the XY block, picker the preset browser. Its
       edit()/saveload() wrappers hardcode a layout that doesn't match this overlay. */
    preset_pages_t presets;
    file_picker_t  scene_picker;               /* whole-panel save/load — its own picker, since it
                                                  caches a different folder listing to presets' */
    bool           scene_dirs_made = false;    /* /PLINKY/u_<panel>/ created (once, on first visit) */
    int            scene_commit_wait = 0;      /* frames left to wait for a staged scene load to complete */
    unsigned char  audio_source = 0;           /* 0 = off (synth only), 1 = line in, 2 = mic */
    unsigned char  audio_in_level = 64;        /* external input level into the chain, 0..127 */
    slider_t       fxslider15;                 /* col-15: bipolar Gravity(up)/Drain(down) */
    unsigned char  fx_val15 = 64;              /* 64 = centre / neutral */
    int            key_pos = 0;                /* circle-of-fifths position 0..11 (left buttons) */
    int            mode_sel = 0;             /* 0..4 = Ionian/Aeolian/Dorian/Lydian/Mixolydian (right buttons) */
    bool           eh_flushed = false;         /* Event Horizon: buffers cleared at the bottom (edge) */
    float          grav_sm = 0.f;              /* Gravity macro, ramped ~2 s (plugin gravitySmooth) */
    int            synth_preset = 0;
    unsigned short voices_active = 0, voices_seen = 0;

    /* visualization taps — written in on_dsp (core1 audio), read in on_ui.
       Each reactive slider is a SELF-CALIBRATING meter: env = fast envelope,
       pk = slow-release peak-hold. on_ui maps env/pk -> brightness (meter_bri),
       so every column pulses across the full brightness range regardless of the
       absolute signal level (loopL, micL and granL differ ~10x in magnitude). */
    float        viz_out = 0.f;                                 /* output level (play-surface breathing) */
    float        viz_loop_env = 0.f,  viz_loop_pk = 0.f;       /* Orbit: main-loop emit meter */
    float        viz_micro_env = 0.f, viz_micro_pk = 0.f;      /* Satellite: micro-loop emit meter */
    float        viz_grain_env = 0.f, viz_grain_pk = 0.f;      /* Constellate: granular meter */
    unsigned int viz_loop = 0, viz_micro = 0;                  /* main-loop & micro-loop phase counters */
    unsigned int viz_loop_len = 1, viz_micro_len = 1;          /* their cycle lengths in samples (falling-star clocks) */
    float        shimmer_phase = 0.f;              /* slow LFO for the Tail slider's shimmer */
    float        freeze_phase = 0.f;               /* Satellite freeze indicator: fast 3-spot bounce */
    float        nav_phase = 0.f, nav_pulse = 0.f; /* nav-bar "you are here" breath (runs on every page) */
    float        preview_mix = 0.f;                /* 0 = normal, 1 = preset audition bypasses the chain */

    /* Self-calibrating meter -> slider brightness (q8, 0..256). Maps a fast
       envelope against its own slow peak-hold: idle -> dim floor, emitting ->
       bright, so it pulses at ANY absolute level. gate_ref = the per-channel
       signal scale (its silence threshold). Verified in the desktop harness. */
    static int meter_bri(float env, float pk, float gate_ref) {
        float d = pk < 0.005f ? 0.005f : pk;
        float n = env / d;        if (n > 1.f) n = 1.f;    /* 0..1 pulse (env vs recent peak) */
        float g = pk / gate_ref;  if (g > 1.f) g = 1.f;    /* dim toward silence */
        int b = 36 + (int)(n * g * 220.f);                 /* 36 (idle) .. 256 (peak) */
        if (b < 0) b = 0; if (b > 256) b = 256;
        return b;
    }

    void setup_default_panel_state() override {

        fx_val[FX_ORBIT] = 48; fx_val[FX_CONSTELLATE] = 48; fx_val[FX_SATELLITE] = 32;
        fx_val[FX_TAIL] = 76; fx_val[FX_FLUX] = 40; fx_val[FX_SPECTRA] = 64; fx_val[FX_MIX] = 90;
        memset(&fx, 0, sizeof fx);
        fx.bpm = 120.f; fx.loop_length_bars = 2.f; fx.key = 0; fx.chord = 0; fx.bloom = 0.4f;
        fx.gravity = 0.f; fx.horizon = 1.f;   /* neutral: no gravity, full sustain */
        /* Explicit, not inherited. Every other control gets its default here, and relying on
           the arena being pre-zeroed is an assumption about someone else's code — the
           "blank panel" action routes through this function too, so an empty grid has to be
           stated rather than assumed. */
        memset(pattern, 0, sizeof pattern);
        memset(pattern_mod, 0, sizeof pattern_mod);
        drum_kit = drum_kit_sel = -1;   /* generated kit */
        drum_mute = 0;
        drum_kit_arm_us = 0;
        drum_step = 0;
        set_drum_transpose(0);
        push_fx_from_ui();
        fx_sm = fx;   /* start the smoother at the target so nothing ramps up from 0 on boot */

        /* Ambiotica IS the FX and owns the output (on_dsp returns true), so do_fx never
         * runs — but zero the synth voices' reverb/delay sends defensively so nothing can
         * bleed into a native bus. */
        set_param_packed(VOICE_PARAM_REVERB_SEND, 0, &synth_presets[synth_preset]);
        set_param_packed(VOICE_PARAM_DELAY_SEND,  0, &synth_presets[synth_preset]);

        /* audio_source has already been restored from the settings file by the time this
           runs, so route the codec to match it. */
        codec_enable_mic(audio_source == 2);

        build_dsp(true);
    }

    /* Allocate the chain. Split out of setup_default_panel_state because a panel LOAD has
       to redo it: the system zeroes and constructs a staged panel, deserialises into it and
       memcpys it over us — and setup_default_panel_state() does NOT run on that staged copy
       (only the init slot gets that). So after a load every module pointer is NULL, dsp_ok
       is false and sram_pool is zeroed; on_dsp falls to its passthrough branch and the whole
       engine is silently gone. Observed as dsp time collapsing from ~1750us to ~139us.
       See on_load_finished(), which is the documented place to rebuild unserialised runtime
       state. The arena cursors reset here, and allocation order is deterministic, so the
       modules land back at the same addresses. */
    void build_dsp(bool first_boot) {
        g_amb_zero_big = first_boot ? 1 : 0;   /* see alloc_prelude.h — skip the 4 MB clear on rebuild */
        g_amb_ps_base = get_psram_ptr(); g_amb_ps_cap = get_psram_size(); g_amb_ps_used = 0;
        g_amb_sr_base = sram_pool;       g_amb_sr_cap = sizeof(sram_pool);  g_amb_sr_used = 0;
        const int sr = (int) AMB_SR;
        const int loopcap = 32 * sr;
        const bool ps = g_amb_ps_cap >= (size_t) 4 * 1024 * 1024;   /* looper 4 MB (+ big modules at high levels) */
        g_amb_region = 1;   /* PSRAM: big buffers (looper/micro/granular; sequential-ish access) */
        if (ps) looper    = looper_create(loopcap, sr);
        if (ps) microloop = microloop_create(sr);
        if (ps) granular  = granular_create(sr);
        /* Kit is ~57 KB of 8-bit PSRAM, synthesised here. Costs a few ms at boot and again on
           a scene load; that is fine, and it is why there are no sample files to install. */
        if (ps) drums     = drums_create(sr);
        g_amb_region = 0;   /* SRAM pool: fast-access modules (dattorro/harmony/drift/bloom) */
        bloom   = bloom_create(sr);
        drift   = drift_create(sr);
        harmony = harmony_create(sr);
        dsp_ok = looper && microloop && granular && bloom && drift && harmony;
        fc_init(&st, 0.7f);
        st.dat = dattorro_create(sr);   /* Dattorro plate (SRAM region); after fc_init zeroes st */
        dsp_ok = dsp_ok && st.dat;
        g_amb_zero_big = 1;
        /* Deliberately NO *_reset() here on a rebuild. looper_reset alone memsets
         * buf_capacity * 2 channels = ~4 MB of PSRAM, and granular/microloop add more.
         * Blasting that much QSPI from core0 starves core1, which fetches its code and its
         * buffers over the same bus — measured as a 658 ms audio block and an instant
         * overrun. (An earlier attempt to skip the clear in the allocator achieved nothing
         * precisely because these calls redid it.)
         * It is also unnecessary: *_create() callocs each module struct, those are below
         * AMB_BIG_ALLOC so they are still zeroed, and the logical state — write_pos,
         * crossfade, envelopes — is therefore already clean. Only the raw audio in the big
         * rings is stale, and that is the previous scene's wash, which the loop overwrites
         * as it records. Continuity there is a fair trade for not stalling the instrument. */
    }

    /* Core0, after a staged load has been committed over us. Everything not serialised is
       now zero, so rebuild it: the DSP chain (see build_dsp), the macro smoother, and any
       runtime latches. Voices are released because the note-ons that started them belonged
       to the panel we just replaced. */
    /* Raw touch, not a widget: a modifier has to be readable by OTHER pads on the same frame,
       and it must not swallow its own press. */
    bool shift_held(int page_y)  const { return get_touch_down(COL_X,      page_y + CTL_DN)  != 0; }
    bool reroll_held(int page_y) const { return get_touch_down(COL_REROLL, page_y + CTL_TOP2) != 0; }
    bool prob_held(int page_y)   const { return get_touch_down(COL_PROB,   page_y + CTL_TOP2) != 0; }
    bool modulo_held(int page_y) const { return get_touch_down(COL_MODULO, page_y + CTL_TOP2) != 0; }
    bool euclid_held(int page_y) const { return get_touch_down(COL_PATTERN, page_y + CTL_TOP2) != 0; }

    /* xorshift32. Only ever drives UI-level randomisation, never audio, so it needs to be
       cheap and non-repeating rather than statistically good. */
    unsigned int rng = 0x2545f491u;
    unsigned int rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

    /* next_prob / prob_label / step_fires live in stepcond.h so they can be unit-tested. */

    /* Blocks' model: each press randomises a QUARTER of the track, so holding reroll and
       tapping repeatedly builds the pattern up rather than replacing it wholesale. */
    void reroll_track(int t) {
        for (int k = 0; k < DRUM_STEPS / 4; k++) {
            int s = (int)(rnd() % DRUM_STEPS);
            pattern[t * DRUM_STEPS + s] = (rnd() & 1) ? (unsigned char)(64 + (rnd() % 64)) : 0;
        }
    }

    /* Reroll + SYNTH. Randomises the CURRENT preset's parameters rather than loading a
       random preset: it needs no bank enumeration, and it is what Toadstep's reroll does on
       its synth pages. The list and its ranges are deliberately curated — pitch, octave and
       volume are left alone because randomising those breaks tuning and levels rather than
       making a new sound, and the envelope ranges lean long because this is an ambient box. */
    void reroll_synth() {
        struct { int param, lo, hi; } r[] = {
            { VOICE_PARAM_CUTOFF_LP,  40, 127 }, { VOICE_PARAM_RESONANCE,   0,  80 },
            { VOICE_PARAM_CUTOFF_HP,   0,  40 }, { VOICE_PARAM_ATTACK,     10, 100 },
            { VOICE_PARAM_DECAY,      40, 127 }, { VOICE_PARAM_SUSTAIN,    40, 127 },
            { VOICE_PARAM_RELEASE,    50, 127 }, { VOICE_PARAM_SUBOSC,     45,  90 },
            { VOICE_PARAM_WAVEFOLD,   45,  90 }, { VOICE_PARAM_CHORUS,     45, 100 },
            { VOICE_PARAM_STEREO,     40, 127 }, { VOICE_PARAM_GLIDE,       0,  40 },
        };
        for (unsigned i = 0; i < sizeof(r) / sizeof(r[0]); i++)
            set_param_packed(r[i].param, r[i].lo + (int)(rnd() % (unsigned)(r[i].hi - r[i].lo + 1)),
                             &synth_presets[synth_preset]);
    }

    /* A step's byte IS its probability: 0 = off, 127 = always, less = sometimes. Velocity is
       held constant so the control stays one-dimensional — "how often", not "how loud".
       This is the thing that stops an ambient pattern repeating identically forever, and it
       is why both Blocks and Toadstep put probability on their step editors. */
    void fire_drum_step() {
        for (int t = 0; t < DRUM_TRACKS; t++) {
            if (drum_mute & (1u << t)) continue;   /* muted: no trigger, tails ring out */
            int i = t * DRUM_STEPS + drum_step;
            if (!step_fires(pattern[i], pattern_mod[i], drum_pass, rnd())) continue;
            drums_trigger(drums, t, 100);
        }
    }

    /* Musical timing, on core0's high-priority timer — it keeps running while the foreground
       thread is blocked on SD, which matters here because a scene save stalls on_ui for 70 ms+
       and the groove must not stutter through it.
       drums_trigger only writes two ints per voice, which core1 reads; a torn read costs at
       worst one block at the wrong amplitude, which is why this needs no lock. */
    void on_sequence(int delta_time_us) override {
        (void)delta_time_us;
        if (!drums) return;
        if (!is_transport_playing()) return;
        /* 4 divider steps per quarter note = 16ths. update() reports how many edges were
           crossed, so a stall cannot silently drop the beat. */
        int edges = drum_clock.update(-1, 4, 1, UPDATE_DIV_ON_BAR);
        if (edges <= 0) return;
        if (has_transport_just_started()) {
            drum_step = 0;                       /* start of the bar, not wherever we stopped */
            drum_pass = 0;                       /* and modulo counts from the first loop */
        } else {
            /* Skip forward over any steps a stall swallowed rather than firing them all at
               once — a burst of stacked hits reads as a glitch, a skipped step does not.
               The pass counter is advanced from the same sum, so a stall that swallows a
               whole loop still moves modulo on rather than silently repeating a pass. */
            int next = drum_step + edges;
            drum_pass += (unsigned int)(next / DRUM_STEPS);
            drum_step = next & (DRUM_STEPS - 1);
        }
        fire_drum_step();
    }

    void on_load_finished() override {
        release_all_voices();
        build_dsp(false);        /* rebuild: skips the multi-MB PSRAM clear, resets instead */
        push_fx_from_ui();
        fx_sm = fx;              /* land on the loaded scene rather than ramping into it */
        eh_flushed = false;
        preview_mix = 0.f;
        /* The scene carried its own presets, so the tracks' slices point at whatever it
           loaded — re-derive them rather than trusting the pointers we just overwrote. */
        refresh_drum_slices();
        printf("SCENE: load committed, dsp_ok=%d\n", (int)dsp_ok);
    }

    void push_fx_from_ui() {
        /* Mirrors the plugin's MacroMap.h::deriveStages exactly — same curves and
         * knob DIRECTIONS — so each control behaves identically (timbre aside). */
        const float orbit  = fx_val[FX_ORBIT]       / 127.f;
        const float tex    = fx_val[FX_CONSTELLATE] / 127.f;
        const float sat    = fx_val[FX_SATELLITE]   / 127.f;
        const float tail   = fx_val[FX_TAIL]        / 127.f;
        const float motion = fx_val[FX_FLUX]        / 127.f;
        const float harm   = fx_val[FX_SPECTRA]     / 127.f;

        fx.loop_length_bars = 0.5f + 7.5f * (1.f - orbit);   /* Orbit: REVERSED (up = shorter) */
        fx.loop_layer       = 0.82f;                          /* kLoopBedLayer: constant bed */
        fx.decay            = 0.30f + 0.70f * tail;           /* Tail: reverb decay */
        fx.ring             = tail;                            /* Tail: chord ring length */
        fx.grain_size       = 0.15f + 0.70f * tex;            /* Constellate: grain size */
        fx.scatter          = tex;                            /* Constellate: scatter */
        fx.mod_depth        = motion;                          /* Flux */
        fx.mod_rate         = 0.10f + 0.80f * motion;
        fx.drift_amt        = motion;
        fx.bloom            = 0.60f;                           /* kBakedBloom: constant swell */
        fx.spectra          = harm;                            /* Spectra: chord amount */
        fx.micro_hold       = sat;                             /* Satellite: hold -> Freeze at top */
        fx.micro_bars       = 0.125f + 1.875f * (1.f - sat);  /* Satellite: micro length, REVERSED */
        fx.mix              = fx_val[FX_MIX] / 127.f;          /* Mix: dry/wet */

        /* Gravity + Event Horizon macros (mirror MacroMap::deriveStages). Gravity is the
         * master "collapse to the drone": as it rises it lerps every stage toward its
         * drone target — the loop bed swells, reverb maxes, the micro-loop freezes, the
         * chord rings, grains thicken. Event Horizon lerps them back toward "clear",
         * applied LAST so it overrides Gravity. grav_sm ramps over ~2 s (plugin value). */
        grav_sm += 0.010f * (fx.gravity - grav_sm);            /* ~2 s (plugin gravitySmooth) */
        { float gd = fx.gravity - grav_sm; if (gd < 5e-4f && gd > -5e-4f) grav_sm = fx.gravity; }  /* settle so fc_push_params can gate off */
        const float gg = grav_sm < 0.f ? 0.f : (grav_sm > 1.f ? 1.f : grav_sm);
        #define AMB_LERP(a,b,t) ((a) + ((b) - (a)) * (t))
        fx.loop_layer = AMB_LERP(fx.loop_layer, 0.97f, gg);
        fx.decay      = AMB_LERP(fx.decay,      1.00f, gg);
        fx.drift_amt  = AMB_LERP(fx.drift_amt,  0.85f, gg);
        fx.micro_hold = AMB_LERP(fx.micro_hold, 1.00f, gg);
        fx.spectra    = AMB_LERP(fx.spectra,    0.60f, gg);
        fx.ring       = AMB_LERP(fx.ring,       1.00f, gg);
        fx.grain_size = AMB_LERP(fx.grain_size, 0.70f, gg);
        fx.scatter    = AMB_LERP(fx.scatter,    0.55f, gg);
        fx.mod_depth  = AMB_LERP(fx.mod_depth,  0.55f, gg);
        float clear = 1.0f - fx.horizon; if (clear < 0.f) clear = 0.f;
        if (clear > 0.f) {
            fx.loop_layer = AMB_LERP(fx.loop_layer, 0.08f, clear);
            fx.micro_hold = AMB_LERP(fx.micro_hold, 0.00f, clear);
            fx.ring       = AMB_LERP(fx.ring,       0.00f, clear);
            fx.decay      = AMB_LERP(fx.decay,      0.00f, clear);
            fx.drift_amt  = AMB_LERP(fx.drift_amt,  0.00f, clear);
        }
        #undef AMB_LERP
    }

    static void note_cb(void* user, int voice, int note, unsigned char vel, finger_t f) {
        ambiotica* self = (ambiotica*)user;
        if (voice < 0 || voice >= 16) return;
        unsigned short bit = (unsigned short)(1u << voice);
        bool is_new = (self->voices_active & bit) == 0;
        int pnote = note - 12; if (pnote < 0) pnote = 0;   /* play surface 1 octave lower */
        play_synth(voice, self->synth_preset, (int)vel, pnote << 8, is_new);
        self->voices_seen |= bit;
        (void)f;
    }

    /* play-surface glow: breathes with the wash. (Grain activity now pulses the
       Constellate slider instead of sparkling here — see on_ui.) */
    static unsigned char viz_brightness(void* user, int si, int sp, int x, int y, int note) {
        (void)si; (void)sp; (void)note; (void)x; (void)y;
        ambiotica* self = (ambiotica*)user;
        int b = (int)(self->viz_out * 28.f);                       /* breathing base glow */
        if (b < 0) b = 0; if (b > 255) b = 255;
        return (unsigned char)b;
    }

    int get_num_pages() override { return PAGE_N; }
    int get_num_panel_settings_pages() override { return SET_N; }

    /* Play-surface colour: hue = key (root), shade = mode. The KEY/BANK control pads
       share it, so the four "what are the strings tuned to" pads visibly belong to the
       surface they retune. Hue is held in 8..15 so it can never collide with a macro
       slider (those use hues 0..6, Orbit = 0). */
    uint32_t key_col() const { return palette[(7 + mode_sel * 2) & 15][8 + (fx.key & 7)]; }

    void apply_key_mode() {
        fx.key   = (key_pos * 7) % 12;   /* circle of fifths -> root semitone offset */
        fx.chord = mode_sel;             /* mode also picks the Spectra wash tonic */
    }

    /* Leaving page 0 stops do_play_surface running, so nothing would ever send the
       note-offs for pads still held as the page scrolled away — release them here or
       they hang for as long as you stay on the editor. */
    void release_all_voices() {
        for (int v = 0; v < 16; v++) if (voices_active & (1u << v)) synth_note_up(v);
        voices_active = voices_seen = 0;
    }

    /* Nav bar — the same four physical pads on every page, so the way between them never
       moves. The pad for the page you are ON breathes; the others sit dim, so "where am I"
       and "where can I go" both read at a glance. Every page draws its content at +UI_Y, so
       the control rows are always free and the whole bar fits on all four pages.
       SCALE doubles as Cancel on the picker pages: on_done() is the picker's own cleanup
       (drops the audition preview and the pending delete), so leaving via SCALE is a real
       cancel rather than just a scroll. */
    /* scroll_to_page ANIMATES, and the incoming page slides upward under whatever finger is
       still down from the tap that started it. Anything that pad passes over fires:
         - nav pads sit on rows 0/14, so tapping TRACKS could land on the preset browser
         - worse, the drums grid sweeps past too, and TRACKS is column 9 — so holding it
           wrote step 10 on nearly every track as the rows went by
       So freeze ALL touch-driven input until the scroll has finished. Everything still
       draws; only actions are suppressed.
       There is no "page settled" callback, but the state is exact: get_scroll_y_16() is the
       live position in 1/16 LED units and page N sits at y = N*16, so it has settled when
       that equals N*256. That is better than guessing a duration — it releases input the
       instant the grid stops rather than a fixed time later.
       The ceiling exists because scroll_settled() rests on my reading of those units: if it
       is wrong and never returns true, input would be dead forever. With the ceiling the
       worst case degrades to a one-second timer. If input feels frozen for a beat after
       every page change, that assumption is what to look at. */
    static constexpr int NAV_FREEZE_CEILING_US = 1000000;
    bool scroll_settled() const { return get_scroll_y_16() == get_scroll_page() * 256; }
    bool input_frozen()  const { return nav_cooldown_us > 0 && !scroll_settled(); }
    void nav_goto(int page) { scroll_to_page(page); nav_cooldown_us = NAV_FREEZE_CEILING_US; }

    void draw_nav(int page_y, int page) {
        uint32_t here = fade_col(WHITE, 90 + (int)(nav_pulse * 166.f)), away = DIMMER(WHITE);
        const bool armed = !input_frozen();
        if (button(COL_SCALE, page_y + CTL_UP, page == PAGE_PLAY ? here : away, ISOLATED,
                   page == PAGE_PRESET || page == PAGE_SCENE ? "Cancel — back to play" : "Play surface")
            && armed) {
            if (page == PAGE_PRESET) presets.picker.on_done();
            if (page == PAGE_SCENE)  scene_picker.on_done();
            nav_goto(PAGE_PLAY);
        }
        /* ⭕ reroll and PROB, on their printed pads. Read with raw touch and drawn with
           set_led for the same reason × is: a modifier has to be legible to OTHER pads on
           the same frame and must not swallow its own press. */
        bool rr = reroll_held(page_y);
        set_led(COL_REROLL, page_y + CTL_TOP2, rr ? PURPLE : DIMMESTEST(PURPLE));
        set_led(COL_PROB,   page_y + CTL_TOP2, prob_held(page_y) ? CYAN : DIMMESTEST(CYAN));
        /* PATTERN, GENERATE's other half. GREEN keeps it distinct from FILL's PURPLE beside
           it and PROB's CYAN, so the three modifiers on this row read apart at a glance.
           Drums page ONLY, unlike FILL beside it: reroll does something everywhere (⭕ +
           SYNTH), but Euclid generates into the step grid and has no meaning off this page.
           A pad stays dark until it has a function that matches its label. */
        if (page == PAGE_DRUMS) {
            set_led(COL_PATTERN, page_y + CTL_TOP2, euclid_held(page_y) ? GREEN : DIMMESTEST(GREEN));
            /* MODULO, CONDITION's other half. Drums page only for the same reason as PATTERN:
               it edits the step grid and means nothing anywhere else. */
            set_led(COL_MODULO, page_y + CTL_TOP2, modulo_held(page_y) ? ORANGE : DIMMESTEST(ORANGE));
        }

        if (button(COL_SYNTH,  page_y + CTL_DN,
                   rr ? PURPLE : (page == PAGE_SYNTH ? here : away), ISOLATED,
                   rr ? "Randomise the synth" : "Synth editor") && armed) {
            if (rr) reroll_synth();          /* ⭕ + SYNTH = new sound, no page change */
            else    nav_goto(PAGE_SYNTH);
        }
        if (button(COL_PRESET, page_y + CTL_TOP, page == PAGE_PRESET ? here : away, ISOLATED, "Synth presets") && armed)
            nav_goto(PAGE_PRESET);
        if (button(COL_SONG,   page_y + CTL_DN,  page == PAGE_SCENE  ? here : away, ISOLATED, "Save/load scene") && armed)
            nav_goto(PAGE_SCENE);
        /* × — the printed shift key, on every page. Drawn with set_led and read with raw
           touch rather than as a widget, so holding it modifies other pads instead of
           consuming the press itself. */
        bool xh = shift_held(page_y);
        set_led(COL_X, page_y + CTL_DN, xh ? WHITE : DIMMESTEST(WHITE));

        if (button(COL_TRACKS, page_y + CTL_UP,
                   xh ? RED : (page == PAGE_DRUMS ? here : away), ISOLATED,
                   xh ? "Clear the whole pattern" : "Drum sequencer") && armed) {
            if (xh) { memset(pattern, 0, sizeof pattern); memset(pattern_mod, 0, sizeof pattern_mod); }
            else    nav_goto(PAGE_DRUMS);
        }

        /* Transport on the printed corner, on every page — the groove has to be startable
           from wherever you are, and these are the pads users already expect it on. */
        bool playing = is_transport_playing();
        if (button(COL_PLAY, page_y + CTL_DN,
                   playing ? fade_col(GREEN, 90 + (int)(nav_pulse * 166.f)) : DIMMER(GREEN),
                   ISOLATED, playing ? "Playing" : "Play"))
            start_transport();
        if (button(COL_STOP, page_y + CTL_DN, playing ? DIMMER(RED) : DIMMESTEST(RED),
                   ISOLATED, "Stop")) {
            stop_transport();
            drums_all_off(drums);   /* kill ringing tails rather than letting them run out */
        }
    }

    /* x0x grid: the whole pattern at once — 8 tracks down, 16 steps across — rather than the
       classic one-track-at-a-time, because we have the rows and seeing it all beats paging.
       Colour is per track (hue) so rows stay tellable apart; every 4th step is dimly lit as a
       beat ruler; the playhead brightens its whole column. */
    /* One-shot dump of the 12 preset slots, on first visit to the drums page. We cannot see
       what is on someone's card from outside, but the panel can: this reports which slots
       hold kits (is_kit = the preset name contains "kit" as a delimited word), how many
       slices carry sample data, and how long the underlying tape is. That is exactly what
       the drum engine needs to point tracks at real samples — one kit preset's 8 slices map
       onto our 8 tracks. User-triggered so it costs nothing until you go looking. */
    /* Sampled kits that ship on the stock SD card. Each bank is one tape plus up to 64
       one-slice presets — one drum per preset — so a kit costs us 8 of the 12 preset slots,
       loaded into 1..8 with slot 0 left for the play surface.
       NOT included: procedural/Transistor_Kit, whose presets start with '!' and have an
       empty tape — the firmware synthesises those through the synth engine, so there is no
       sample memory for us to read and they would have to go through the wash. */
    struct drum_kit_t {
        const char* category;
        const char* bank;
        int         preset_in_bank;   /* <0 = one-shot kit (8 presets -> 8 tracks);
                                         >=0 = SLICED: this one preset, cut into 8 */
        const char* label;            /* 4 chars — all that fits across 16 columns in FONT_4 */
        const char* name;             /* full name, for the help line / second screen */
    };
    static constexpr drum_kit_t kKits[] = {
        { "2_DRUMS", "ELEC_KIT",      -1, "ELEC", "Elec kit"      },
        { "2_DRUMS", "ALUMINIUM_KIT", -1, "ALUM", "Aluminium kit" },
        { "2_DRUMS", "MIAMI_KIT",     -1, "MIAM", "Miami kit"     },
        /* Breaks. Every DivBeats preset is exactly 113778 samples = one bar, so cutting it
           into DRUM_TRACKS gives eight eighth-notes: track n is the nth eighth of the bar,
           and the step grid rearranges them. One preset slot, not eight. Bank indices are
           the octal filenames (20.json = 16, ...). */
        { "artists", "DivBeats",  0, "AMEN", "Amen Brother"      },
        { "artists", "DivBeats",  1, "ASHL", "Ashleys Roachclip" },
        { "artists", "DivBeats",  2, "COLD", "Cold Sweat"        },
        { "artists", "DivBeats",  3, "FUNK", "Funky Drummer"     },
        { "artists", "DivBeats",  4, "PRES", "Funky President"   },
        { "artists", "DivBeats",  5, "IMPC", "Impeach President" },
        { "artists", "DivBeats",  6, "ROCK", "Rock Steady"       },
        { "artists", "DivBeats",  7, "THNK", "Think About U"     },
        { "artists", "DivBeats", 16, "DUB",  "Dubstep"           },
        { "artists", "DivBeats", 17, "ELKT", "Elektro Break"     },
        { "artists", "DivBeats", 18, "TRON", "Electronica"       },
        { "artists", "DivBeats", 19, "HIP",  "Hip Hop"           },
        { "artists", "DivBeats", 20, "HOUS", "House"             },
        { "artists", "DivBeats", 21, "LOFI", "LoFi Garage"       },
        { "artists", "DivBeats", 22, "TECH", "Techno"            },
        { "artists", "DivBeats", 23, "DNB",  "Drum and Bass"     },
    };
    static constexpr int kNumKits = (int)(sizeof(kKits) / sizeof(kKits[0]));

    /* Re-point each track at its preset's slice, or clear it so the generated voice plays.
       Called after loading a kit and after a scene load, since a load replaces the presets. */
    void refresh_drum_slices() {
        if (!drums) return;
        /* build_dsp() makes a fresh drums_t at unity, so a loaded scene's transpose has to be
           pushed back in here — this runs after every rebuild and every kit change. */
        drums_set_pitch(drums, (float)drum_transpose);
        const bool sliced = drum_kit >= 0 && kKits[drum_kit].preset_in_bank >= 0;
        /* A sliced break is one continuous performance, so the hat choke — which makes a kit
           sound like a kit — would just cut adjacent eighths off each other. */
        drums_set_choke(drums, !sliced);

        if (sliced) {
            const synth_preset_t* p = &synth_presets[DRUM_PRESET_BASE];
            const synth_preset_slice_t* sl = &p->slice[0];
            if (preset_slice_has_sample_data(sl)) {
                /* Slice offsets are RELATIVE to sample_data_va — confirmed on device, where
                   sample_data_va reads 8421376 and mip-0 playback is correct, which an
                   absolute address could not be. Kept as offsets from here on so the chops
                   can be handed to drums_set_sample_mipped and reach the prefiltered
                   octaves; see dsp/mipmap.h. */
                unsigned int a = sl->start_read_only, b = sl->end_read_only;
                /* Length comes from the preset's OWN chop grid, not from the track count.
                   The breaks ship no explicit chop_count, so they fall through to
                   PRESET_DEFAULT_CHOP_COUNT (16) — sixteenths. Cutting the bar into
                   DRUM_TRACKS instead gave eighths, twice the intended length, and every
                   hit ran into the next.
                   Chops are CONTIGUOUS rather than spread across the bar: tracks 0..7 are
                   eight consecutive sixteenths, so playing them in order replays the first
                   half-bar and the grid genuinely rearranges it. Spreading them over the
                   whole bar would cover more of the break but only sample the downbeat of
                   each eighth, which is less useful than a run you can actually reorder. */
                unsigned int bar  = b - a;
                unsigned int chop = bar / preset_slice_chop_count(sl);
                for (int t = 0; t < DRUM_TRACKS; t++) {
                    unsigned int s0 = a + chop * t;
                    unsigned int s1 = s0 + chop;
                    if (s0 >= b) { drums_set_sample(drums, t, 0, 0); continue; }  /* short sample */
                    if (s1 > b) s1 = b;
                    drums_set_sample_mipped(drums, t, p->sample_data_va,
                                            p->tape_length_samples, s0, s1);
                }
                return;
            }
        }
        for (int t = 0; t < DRUM_TRACKS; t++) {
            const synth_preset_t* p = &synth_presets[DRUM_PRESET_BASE + t];
            const synth_preset_slice_t* sl = &p->slice[0];
            if (drum_kit >= 0 && preset_slice_has_sample_data(sl))
                drums_set_sample_mipped(drums, t, p->sample_data_va, p->tape_length_samples,
                                        sl->start_read_only, sl->end_read_only);
            else
                drums_set_sample(drums, t, 0, 0);   /* clear: fall back to the generated voice */
        }
    }

    void set_drum_transpose(int semis) {
        if (semis < -24) semis = -24;
        if (semis >  24) semis =  24;
        drum_transpose = semis;
        drums_set_pitch(drums, (float)semis);
    }

    static int wrap_kit(int k) { return k < -1 ? kNumKits - 1 : (k >= kNumKits ? -1 : k); }
    const char* kit_label(int k) const { return k < 0 ? "GEN"       : kKits[k].label; }
    const char* kit_name (int k) const { return k < 0 ? "Generated" : kKits[k].name;  }

    /* Move the selection and restart the arming timer. 700 ms is long enough to scroll
       several entries without committing, short enough not to feel stuck. */
    void arm_kit(int k) { drum_kit_sel = wrap_kit(k); drum_kit_arm_us = 700000; }

    /* Runs from on_ui, not the drums page, so an armed selection still commits if you
       navigate away mid-scroll rather than being silently abandoned. */
    void tick_kit_arm(int dt_us) {
        if (drum_kit_arm_us <= 0) return;
        drum_kit_arm_us -= dt_us;
        if (drum_kit_arm_us > 0) return;
        drum_kit_arm_us = 0;
        if (drum_kit_sel != drum_kit) load_drum_kit(drum_kit_sel);
    }

    /* kit = -1 is our generated kit; 0..kNumKits-1 load a sampled bank into slots 1..8.
       NB loading presets runs the system's chunk planner, which blocks core0 for tens of ms
       and drops a step or two — a between-takes gesture, not a performance one. */
    void load_drum_kit(int kit) {
        kit = wrap_kit(kit);
        drum_kit = drum_kit_sel = kit;
        drums_all_off(drums);
        if (kit >= 0) {
            const drum_kit_t& k = kKits[kit];
            if (k.preset_in_bank >= 0)   /* sliced break: one preset, cut into 8 below */
                load_preset_from_filename_with_category(DRUM_PRESET_BASE, k.category, k.bank, k.preset_in_bank);
            else                         /* one-shot kit: one preset per track */
                for (int t = 0; t < DRUM_TRACKS; t++)
                    load_preset_from_filename_with_category(DRUM_PRESET_BASE + t, k.category, k.bank, t);
        }
        refresh_drum_slices();
#ifdef AMB_PROFILE
        /* Both layouts put a sampled slice at DRUM_PRESET_BASE — the break in the sliced
           case, track 0's one-shot otherwise — so one probe covers both. */
        if (kit >= 0) {
            const synth_preset_t* p = &synth_presets[DRUM_PRESET_BASE];
            if (preset_slice_has_sample_data(&p->slice[0])) report_mips(p, &p->slice[0]);
        }
#endif
    }

    void report_presets() {
        if (preset_report_done) return;
        preset_report_done = true;
        /* Steps that were never entered have shown up after a load; count them so we can see
           whether the pattern really holds data or the grid is only drawing it that way. */
        int lit = 0, first = -1;
        for (int i = 0; i < (int)sizeof(pattern); i++)
            if (pattern[i]) { lit++; if (first < 0) first = i; }
        printf("DRUMS: lit=%d first=%d (trk %d step %d) kit=%d\n",
               lit, first, first < 0 ? -1 : first / DRUM_STEPS,
               first < 0 ? -1 : first % DRUM_STEPS, drum_kit);
        for (int i = 0; i < MAX_SYNTH_PRESETS; i++) {
            const synth_preset_t* p = &synth_presets[i];
            int slices = 0;
            for (int s = 0; s < PRESET_SLICE_COUNT; s++)
                if (preset_slice_has_sample_data(&p->slice[s])) slices++;
            printf("P%02d kit=%d slices=%d tape=%u '%s' bank='%s/%s'\n",
                   i, (int)synth_preset_is_kit(p), slices,
                   (unsigned)p->tape_length_samples, p->name, p->bank_category, p->bank_filename);
        }
    }

#ifdef AMB_PROFILE
    /* Mip probe. Answers the two things the SDK reference does not: where a slice lives in
       mip space, and whether the mip pyramid is even RESIDENT.
       The addressing is settled by argument already — get_mip_va(p,0,false) IS
       sample_data_va, so today's working mip-0 playback proves start_read_only cannot be an
       absolute VA unless sample_data_va is 0, and in that case both readings are the same
       number. Either way base(m) + (off >> m) holds. This prints sample_data_va so the
       argument becomes a measurement.
       Residency is the real risk: "Plinky will try to load only the needed parts of each
       bank's sample data", and reading far past a sample "may get crashes". So sample each
       mip and report its peak — all-zero above mip 0 means the pyramid is not paged in and
       mipmapping must stay off. Reads stay inside the computed mip region, which is inside
       the tape, so the probe itself cannot run off the end. */
    void report_mips(const synth_preset_t* p, const synth_preset_slice_t* sl) {
        unsigned int off0 = sl->start_read_only, off1 = sl->end_read_only;
        printf("MIP sample_data_va=%u tape=%u slice=[%u,%u) len=%u\n",
               (unsigned)p->sample_data_va, (unsigned)p->tape_length_samples,
               off0, off1, off1 - off0);
        for (unsigned m = 0; m <= 8; m++) {
            unsigned int base = get_mip_va(p, m, false);
            /* dsp/mipmap.h REIMPLEMENTS get_mip_va, because drums.c also builds in the
               desktop harness where the SDK does not exist. Cross-check the copy against the
               real thing here, so a future SDK change shows up as a loud mismatch on the next
               kit load rather than as quietly wrong sample addresses. */
            unsigned int ours = mip_base(p->sample_data_va, p->tape_length_samples, m);
            if (ours != base) printf("  !! mip%u MISMATCH ours=%u sdk=%u\n", m, ours, base);
            unsigned int va   = base + (off0 >> m);
            unsigned int n    = (off1 - off0) >> m;
            int peak = 0;
            for (unsigned k = 0; k < 128 && k < n; k++) {
                int v = (int)(int16_t)READ_SAMPLE(va + (n * k) / 128u);
                if (v < 0) v = -v;
                if (v > peak) peak = v;
            }
            printf("  mip%u base=%u va=%u len=%u peak=%d\n", m, base, va, n, peak);
        }
    }
#endif

    void draw_drums_page() {
        const int page_y = PAGE_DRUMS * 16;
        report_presets();
        bool playing = is_transport_playing();
        /* Tap toggles, drag paints. The FIRST pad of a gesture decides the mode from its own
           state — land on an empty step and the whole drag writes, land on a lit one and it
           erases — and that mode is held until every finger lifts. That is what makes a tap
           behave like a toggle while a drag stays coherent: without the latch, each pad the
           finger crossed would flip on its own and a swipe would just invert the row.
           × forces erase regardless, so you can scrub out a run that starts on a gap. */
        bool erase_mod = shift_held(page_y);
        bool prob_mod  = prob_held(page_y);
        bool rr_mod    = reroll_held(page_y);
        bool eu_mod    = euclid_held(page_y);
        bool mod_mod   = modulo_held(page_y);
        /* The selection belongs to a single hold: let go of both CONDITION pads and the next
           tap inspects again rather than silently advancing whatever was last touched. */
        if (!prob_mod && !mod_mod) cond_sel = -1;
        /* MUTE: hold + tap a track toggles it; a tap on its own unmutes everything, which is
           how every Plinky panel's mute behaves. "On its own" is tracked across the hold
           rather than guessed at, so a hold that did toggle something does not also
           un-mute-all when released. */
        const int mute_y = page_y + ROW_MUTE;
        bool mute_mod = get_touch_down(COL_MUTE, mute_y) != 0;
        if (get_touch_pressed(COL_MUTE, mute_y)) mute_hit_track = false;
        if (get_touch_released(COL_MUTE, mute_y) && !mute_hit_track) drum_mute = 0;
        bool any_down  = false;
        for (int t = 0; t < DRUM_TRACKS; t++) {
            int y = page_y + UI_Y + t;
            for (int s = 0; s < DRUM_STEPS; s++) {
                int idx = t * DRUM_STEPS + s;
                /* Frozen during a page transition — otherwise the finger still down from the
                   TRACKS tap writes a step on every row the grid slides past it. */
                if (!input_frozen() && get_touch_down(s, y)) {
                    any_down = true;
                    if (mute_mod) {
                        if (!drum_paint) {
                            drum_paint = 3; mute_hit_track = true;
                            drum_mute ^= (unsigned char)(1u << t);
                        }
                    } else if (rr_mod) {
                        /* ⭕ + a track randomises a quarter of it. Latched through drum_paint
                           so a held finger rerolls once rather than every frame. */
                        if (!drum_paint) { drum_paint = 3; reroll_track(t); }
                    } else if (eu_mod) {
                        /* PATTERN + press = a Euclidean rhythm of (column + 1) pulses,
                           replacing the track. Keep holding and slide along the row to
                           rotate it; the grid redraws from pattern[] every frame, so the
                           phase moves live under the finger.
                           This is the ONE modifier here that must keep acting while
                           drum_paint == 3, so the press and the drag are two separate
                           conditions rather than the usual single !drum_paint guard —
                           with that guard you would get pulse selection and no rotation.
                           eu_track pins the gesture to the row it started on, so a finger
                           wandering vertically cannot silently rewrite a second track. */
                        if (!drum_paint) {
                            drum_paint = 3;
                            eu_track  = (signed char)t;
                            eu_anchor = (signed char)s;
                            eu_last   = (signed char)s;
                            eu_k      = (unsigned char)(s + 1);
                            euclid_fill(&pattern[t * DRUM_STEPS], eu_k, 0);
                        } else if (eu_track == (signed char)t && eu_last != (signed char)s) {
                            eu_last = (signed char)s;
                            euclid_fill(&pattern[t * DRUM_STEPS], eu_k, s - eu_anchor);
                        }
                    } else if (prob_mod || mod_mod) {
                        /* The two CONDITION pads share one gesture. The FIRST tap on a step
                           only selects it, so the readout below the grid shows what it is
                           already set to; each further tap on that same step advances the
                           ring. Without the inspect step there is no way to read a value
                           without changing it, and modulo in particular is invisible on the
                           surface — 1:4 and 2:4 light identically except on the pass they
                           are due. Moving to another step goes back to inspecting.
                           Only lit steps cycle: an empty step has nothing to make less
                           likely or less frequent, so this never turns steps on by accident. */
                        if (!drum_paint) {
                            drum_paint = 3;
                            if (cond_sel != (short)idx)      cond_sel = (short)idx;
                            else if (pattern[idx]) {
                                if (prob_mod) pattern[idx]     = next_prob(pattern[idx]);
                                else          pattern_mod[idx] = next_mod(pattern_mod[idx]);
                            }
                        }
                    } else {
                        if (!drum_paint) drum_paint = pattern[idx] ? 2 : 1;   /* latch on first contact */
                        /* Mode 3 means a MODIFIER owns this gesture, so releasing that
                           modifier while the finger is still down must not degrade into
                           painting — the latch is "held until every finger lifts", and that
                           has to include the case where the modifier goes up first. Without
                           this guard, lifting PATTERN mid-drag repaints every step the finger
                           then crosses, overwriting the pattern it just generated. The same
                           leak applied to FILL, PROB and MUTE; it was simply hard to reach
                           before a modifier gesture involved dragging. */
                        if (drum_paint != 3)
                            pattern[idx] = (erase_mod || drum_paint == 2) ? 0 : 127;
                    }
                }
                unsigned char vel = pattern[idx];
                bool head = playing && s == drum_step;
                bool muted = (drum_mute & (1u << t)) != 0;
                uint32_t c;
                if (muted) {
                    /* A muted track still shows its pattern, just dimmed — you need to see
                       what you are about to bring back in. Red while MUTE is held, so the
                       gesture reads before you commit to it. */
                    c = vel ? (mute_mod ? DIMMER(RED) : DIMMESTEST(WHITE)) : 0;
                } else if (vel) {
                    /* Brightness carries probability, so holding PROB turns the grid into a
                       readout of how often each step fires rather than a separate page.
                       A step with a modulo condition is dimmed on the passes it is NOT due,
                       so a 1:4 visibly breathes across four bars instead of looking like a
                       plain hit that mysteriously does not sound. */
                    int sh = 3 + (vel * 6) / 127; if (head) sh += 4; if (sh > 15) sh = 15;
                    if (!mod_due(pattern_mod[idx], drum_pass)) sh = sh > 6 ? sh - 4 : 2;
                    c = palette[sh][(t * 2 + 1) & 15];
                }
                else if (head)         c = DIMMER(WHITE);               /* playhead over an empty step */
                else if ((s & 3) == 0) c = DIMMESTEST(WHITE);           /* beat ruler */
                else                   c = 0;
                set_led(s, y, c);
            }
        }
        /* The gesture ends only when the grid is fully released. eu_track is cleared in the
           same breath so a PATTERN drag cannot resume against a row the finger has left. */
        if (!any_down) { drum_paint = 0; eu_track = -1; }
        else if (eu_track >= 0)
            set_help_text("Euclid #fc2#*%d/%d#. rot %+d", eu_k, DRUM_STEPS,
                          ((eu_last - eu_anchor + 8) & 15) - 8);

        /* BANK ▲▼ cycles kits here — which is what the pad is actually printed for. On the
           play page the same pads pick the musical mode; context decides, and both readings
           are honest. The generated kit sits in the ring as -1, so it is always reachable
           and the panel still works with no samples loaded. */
        uint32_t kitcol = drum_kit_sel < 0 ? DIMMER(WHITE) : palette[9][(drum_kit_sel * 3 + 2) & 15];
        if (button(COL_BANK, page_y + CTL_UP, kitcol, ISOLATED, "Kit up"))   arm_kit(drum_kit_sel + 1);
        if (button(COL_BANK, page_y + CTL_DN, kitcol, ISOLATED, "Kit down")) arm_kit(drum_kit_sel - 1);

        /* KEY ▲▼ transposes the kit here — the same pads that move the musical key on the
           play page, doing the pitch job on this one. Resampling, so pitching a break up
           shortens it too: chops get tighter as they get higher, which is the useful
           direction. × + KEY returns to unity rather than making you count back. */
        uint32_t trcol = drum_transpose ? palette[10][drum_transpose > 0 ? 5 : 1] : DIMMER(WHITE);
        if (button(COL_KEY, page_y + CTL_UP, trcol, ISOLATED, "Drums up a semitone"))
            set_drum_transpose(erase_mod ? 0 : drum_transpose + 1);
        if (button(COL_KEY, page_y + CTL_DN, trcol, ISOLATED, "Drums down a semitone"))
            set_drum_transpose(erase_mod ? 0 : drum_transpose - 1);

        /* While the selection is armed, show its name in the four empty rows BELOW the step
           grid, so scrolling kits never hides the pattern you are editing. Four characters
           is all that fits across 16 columns in FONT_4; the full name goes to the help line
           for the second-screen view. */
        if (drum_kit_arm_us > 0) {
            leds_draw_string(0, page_y + UI_Y + DRUM_TRACKS, FONT_4, BLUE, kit_label(drum_kit_sel));
            set_help_text("Kit: #fc2#*%s#.", kit_name(drum_kit_sel));
        }

        /* What the selected step is set to, in the four free rows below the grid — drawn over
           the kit label because if you are holding a CONDITION pad, that is what you are
           reading. Colour follows the pad you are holding, so the number needs no units. */
        if (cond_sel >= 0 && (prob_mod || mod_mod)) {
            const char* txt = prob_mod ? prob_label(pattern[cond_sel])
                                       : mod_label(pattern_mod[cond_sel]);
            leds_draw_string(0, page_y + UI_Y + DRUM_TRACKS, FONT_4,
                             prob_mod ? CYAN : ORANGE, txt);
            if (prob_mod) set_help_text("Step chance: #fc2#*%s%%#.", txt);
            else          set_help_text("Step modulo: #fc2#*%s#. of the loop", txt);
        }

        /* Drawn after the kit-name overlay, which spans these rows while a kit is armed and
           would otherwise paint over it. */
        set_led(COL_MUTE, mute_y, drum_mute ? RED : (mute_mod ? DIMMER(RED) : DIMMESTEST(RED)));

        draw_nav(page_y, PAGE_DRUMS);
    }

    /* Page 0's reserved rows: the tuning pads plus the nav bar. Drawn last so it wins any
       pad the main UI also touched. */
    void draw_control_rows() {
        uint32_t kc = key_col();
        if (button(COL_KEY,  CTL_UP, kc, ISOLATED, "Key up (fifth)"))     { key_pos = (key_pos + 1)  % 12; apply_key_mode(); }
        if (button(COL_KEY,  CTL_DN, kc, ISOLATED, "Key down (fourth)"))  { key_pos = (key_pos + 11) % 12; apply_key_mode(); }
        if (button(COL_BANK, CTL_UP, DIMMER(kc), ISOLATED, "Mode up"))    { mode_sel = (mode_sel + 1) % 5; apply_key_mode(); }
        if (button(COL_BANK, CTL_DN, DIMMER(kc), ISOLATED, "Mode down"))  { mode_sel = (mode_sel + 4) % 5; apply_key_mode(); }
        draw_nav(PAGE_PLAY * 16, PAGE_PLAY);
    }

    /* Stock synth-parameter editor. Offset by UI_Y so its two 5-high slider banks and the
       flag-button row land on the printed pad circles (rows 2..12) and the control rows
       stay clear. NB it also exposes DELAY_SEND / REVERB_SEND and the MIX params, which do
       nothing here — on_dsp owns the output, so the native FX buses never run. */
    /* Synth page laid out to match the STOCK CHORDS synth page, not preset_pages_t::edit()
       (whose two-16-wide-banks layout is toadstep's). The Chords manual gives the regions
       exactly — upper sliders 0,2..16,7 · lower 0,7..8,14 · XY pad 9,7..16,14 — which
       leaves col 8 rows 7..13 as the XY button column, hence XY_BUTTONS_ON_LEFT.
       The payoff is the same one as the control pads: every printed label on the hardware
       is CORRECT for us. The order below is read straight off the overlay silkscreen.
       Slider colours come from each param's own metadata (col = 0), so the printed colour
       groups (the red ADSR cluster, etc.) line up for free.
       Not included: the SIMPLE/TUNE/CHOP/LOOP/SYNC/LPG flag buttons that edit() adds —
       the Chords layout has no printed home for them inside rows 2..13. */
    void draw_synth_page() {
        static const int kTop[16] = {           /* rows 2..6, 16 sliders 5 high */
            VOICE_PARAM_ATTACK,                 /* A     */
            VOICE_PARAM_DECAY,                  /* D     */
            VOICE_PARAM_SUSTAIN,                /* S     */
            VOICE_PARAM_RELEASE,                /* R     */
            VOICE_PARAM_STEREO,                 /* PAN   */
            VOICE_PARAM_SUBOSC,                 /* SUB   */
            VOICE_PARAM_CUTOFF_HP,              /* HP    */
            VOICE_PARAM_CUTOFF_LP,              /* LP    */
            VOICE_PARAM_RESONANCE,              /* RESO  */
            VOICE_PARAM_DELAY_SEND,             /* DELAY */
            MIX_PARAM_DELAY_TIME + 128,         /* TIME  */
            MIX_PARAM_DELAY_FEEDBACK + 128,     /* FBK   */
            VOICE_PARAM_REVERB_SEND,            /* VERB  */
            MIX_PARAM_REVERB_FEEDBACK + 128,    /* TAIL  */
            MIX_PARAM_REVERB_SHIMMER + 128,     /* GLOW  */
            VOICE_PARAM_VOLUME,                 /* VOL   */
        };
        static const int kBot[8] = {            /* rows 7..13, 8 sliders 7 high */
            VOICE_PARAM_GLIDE,                  /* GLIDE  */
            VOICE_PARAM_PITCH,                  /* PITCH  */
            VOICE_PARAM_OCTAVE,                 /* OCT    */
            VOICE_PARAM_CHORUS,                 /* CHORUS */
            VOICE_PARAM_WAVEFOLD,               /* FOLD   */
            VOICE_PARAM_SAMPLE_START,           /* START  */
            VOICE_PARAM_SAMPLE_LENGTH,          /* END    */
            VOICE_PARAM_TIMESTRETCH,            /* SPEED  */
        };
        const int page_y = PAGE_SYNTH * 16, top_y = page_y + UI_Y, bot_y = top_y + 5;
        synth_param_sliders_block(presets.slider_banks[0], 0, top_y, 16, 5, synth_preset, kTop);
        synth_param_sliders_block(presets.slider_banks[1], 0, bot_y,  8, 7, synth_preset, kBot);
        /* x = 8 is the button column (MOD X / Y / loop on the overlay); the pad fills 9..15. */
        synth_xy_block(&presets.the_xy_pad, synth_preset, 8, bot_y, 7, 7,
                       WHITE, BLUE, false, 0, XY_BUTTONS_ON_LEFT);
        draw_nav(page_y, PAGE_SYNTH);
    }

    void set_audio_source(int source) {
        unsigned char next = (unsigned char)clampi(source, 0, 2);
        if (next == audio_source) return;
        audio_source = next;
        codec_enable_mic(audio_source == 2);
        (void)save_settings_to_sd(false);
    }

    /* Panel settings pages (right-up from the play surface), drawn in the stock system style
       so they navigate the way the built-in pages do. The left buttons edit the value.
       These are panel PREFERENCES — they persist via on_serialise_settings and come back on
       boot, unlike the scene, which is saved per slot by on_serialise. */
    void draw_settings_page(int page) {
        if (page == SET_SOURCE) {
            /* "off" keeps the historical behaviour: the chain hears only the Plinky synth. */
            static const char* const src_options[] = { "off", "line", "mic" };
            set_audio_source((int)audio_source + draw_system_style_enum_settings_page("src", audio_source, src_options, 3));
        } else if (page == SET_IN_LEVEL) {
            char buf[8]; snprintf(buf, sizeof buf, "%d", (int)audio_in_level);
            int delta = draw_system_style_settings_page("in", buf, (audio_in_level * 100) / 127);
            if (delta) {
                audio_in_level = (unsigned char)clampi((int)audio_in_level + delta, 0, 127);
                (void)save_settings_to_sd(false);
            }
        }
        /* anything further up is a global system page — leave it alone, it draws itself. */
    }

    /* The two file-picker pages, built from the picker's own pieces rather than the stock
       saveload() wrapper — that wrapper hardcodes its buttons to (14,15)/(15,15), which on
       this overlay is the transport corner. Here the grid sits at +UI_Y (on the printed pad
       circles, file grid + hue row) and the commit buttons land under the printed SAVE and
       LOAD. Both self-colour: dark when the action isn't available, so an empty slot can't
       be loaded and a protected slot can't be overwritten. Cancel is SCALE, via draw_nav. */
    void draw_picker_page(int page, bool scene) {
        const int page_y = page * 16, grid_y = page_y + UI_Y;
        bool done = false;
        if (scene) {
            scene_picker.panel_picker(grid_y, grid_y + 8);
            /* A brand-new panel has no /PLINKY/u_<panel>/ on the card, and nothing creates it
               for us: the picker just fails its folder scan every frame (flooding the log,
               and clearing current_file_idx again right after a tap, so the save/load buttons
               light for one frame and go dark). make_dirs() is the SDK's fix for this. Run it
               after the first panel_picker call so the picker's root is already set, and only
               once — it touches the SD card. */
            /* The folder is created BY SAVING — the runtime makes it on demand, so there is
               nothing to provision up front. It only looked broken because the old 17-char
               panel name overflowed the picker's char[17], which killed the scan and left
               the save button dark. One-shot print of the name and its length so a future
               rename that creeps back over 16 shows up immediately instead of silently
               breaking save/load again. */
            /* (Was printing the panel name and its length here. Answered: 'u_ambiotica',
               11 of the 16 the picker's char[17] allows. Dropped to keep the log quiet
               around a load — see the note in on_ui.) */
            done  = scene_picker.panel_save_button(COL_SAVE, page_y + CTL_UP);
            /* panel_load_button only STAGES the load. Do NOT finalise here: the documented
               precondition is is_panel_load_staged(), which "returns true while a staged
               panel load is COMPLETE and waiting for a commit request" — committing in the
               same frame as the button races the deserialise. Poll for it instead (top of
               on_ui), which also works after we scroll back to the play surface. */
            if (scene_picker.panel_load_button(COL_LOAD, page_y + CTL_UP)) {
                scene_commit_wait = 250;   /* ~1 s of frames to see the stage complete */
                done = true;
            }
        } else {
            presets.picker.preset_picker(synth_preset, grid_y, grid_y + 8);
            done = presets.picker.preset_save_button(synth_preset, COL_SAVE, page_y + CTL_UP)
                 | presets.picker.preset_load_button(synth_preset, COL_LOAD, page_y + CTL_UP);
        }
        if (done) scroll_to_page(PAGE_PLAY);
        draw_nav(page_y, page);
    }

    void on_ui(int dt_us) override {
#ifdef AMB_PROFILE
        /* Per-stage core1 timing (avg us/block). Prints UNCONDITIONALLY ~8x/sec so it's a
         * heartbeat too: if this never appears, printf/on_ui isn't reaching the console;
         * if it shows (n=0) the DSP block isn't completing; otherwise it's the breakdown. */
        static unsigned prof_ctr = 0;
        if ((++prof_ctr % 30) == 0) {
            unsigned nn = g_stage_n ? g_stage_n : 1;
            printf("STG loop=%u gran=%u mic=%u rev=%u harm=%u mix=%u push=%u drum=%u (n=%u)\n",
                   g_stage_us[0]/nn, g_stage_us[1]/nn, g_stage_us[2]/nn, g_stage_us[3]/nn,
                   g_stage_us[4]/nn, g_stage_us[5]/nn, g_stage_us[6]/nn, g_stage_us[7]/nn,
                   g_stage_n);
            for (int s = 0; s < 8; s++) g_stage_us[s] = 0;
            g_stage_n = 0;
        }
#endif
        /* The WHOLE panel object must fit the 128 KB panel arena — and a panel LOAD builds a
           SECOND copy in the shadow arena before memcpying it over us. So an oversized object
           is invisible during normal play and only corrupts memory when you load, which is
           the symptom being chased (commit never completes, USB dies). sram_pool is the
           adjustable 88 KB of it; preset_pages_t and the two file_picker_t caches are several
           KB more. Anything near or over 131072 is the bug.
           Answered: 127040 of 131072, so the object fits and this is no longer worth
           printing periodically. Every observed failure has been a MID-TOKEN truncation of
           the log during the heaviest burst the firmware emits (plantime prints a long line
           per preset slot, and mask=0xfff is twelve of them), which looks like the USB CDC
           stream giving out rather than a hang — so our own output is now kept to the bare
           minimum around a load, to stop us adding to that pressure.
           Kept at a 30 s trickle because the arena is the tightest budget we have and it is
           the one number worth watching as features land. NB it measures the panel OBJECT
           (members) only — sample buffers live in PSRAM and code lives in flash, neither of
           which is counted here. */
        static unsigned size_report_us = 0;
        size_report_us += (unsigned)dt_us;
        if (size_report_us >= 30000000u) {
            size_report_us = 0;
            printf("PANEL: sizeof=%u free=%d dsp_ok=%d\n",
                   (unsigned)sizeof(*this), 131072 - (int)sizeof(*this), (int)dsp_ok);
        }

        if (nav_cooldown_us > 0) nav_cooldown_us -= dt_us;
        tick_kit_arm(dt_us);

        /* Commit a staged scene load once the system reports it complete. Polled here rather
           than at the button because the precondition is not satisfied in the same frame, and
           because we scroll back to the play surface as soon as it is staged. Bounded so a
           stage that never completes (e.g. reloading the slot that is already live, where
           there may be nothing to do) cannot leave a commit armed indefinitely. */
        if (scene_commit_wait > 0) {
            if (is_panel_load_staged()) {
                scene_commit_wait = 0;
                bool accepted = scene_picker.request_panel_load_finalise();
                printf("SCENE: finalise accepted=%d\n", (int)accepted);
                /* The docs say to "return immediately if it accepts" — an accepted request
                   queues a swap of this whole panel object, so carrying on to draw widgets
                   and touch the picker means running against state about to be replaced. */
                if (accepted) return;
            } else if (--scene_commit_wait == 0) {
                printf("SCENE: load never staged, commit abandoned\n");
            }
        }

        /* Nav "you are here" breath (~0.8 Hz). Advanced before the page dispatch so it keeps
           running on every page, not just the play surface. */
        nav_phase += (float)dt_us * 1e-6f * 0.8f; nav_phase -= (float)(int)nav_phase;
        float nt = nav_phase < 0.5f ? nav_phase * 2.f : 2.f - nav_phase * 2.f;
        nav_pulse = nt * nt * (3.f - 2.f * nt);

        /* Page dispatch. Negative pages are the system's own settings UI — return without
           clearing so it keeps its own drawing. The chain runs on core1 regardless of the
           page, so the wash keeps going while you edit the synth or browse presets. */
        /* Orbit and Satellite are bar-length delays derived from bpm (see fc_push_params), so
           this has to track the system tempo. Updated BEFORE the page dispatch: it used to
           live in the page-0 branch, which meant nudging the tempo from the drums page left
           the loop lengths stale until you navigated back. */
        fx.bpm = get_tempo_bpm(); if (fx.bpm < 1.f) fx.bpm = 120.f;

        int page = get_scroll_page();
        if (page < 0) { draw_settings_page(page); return; }
        leds_clear();
        if (page != PAGE_PLAY) {
            release_all_voices();
            if (page == PAGE_DRUMS)       draw_drums_page();
            else if (page == PAGE_SYNTH)  draw_synth_page();
            else if (page == PAGE_PRESET) draw_picker_page(PAGE_PRESET, false);
            else if (page == PAGE_SCENE)  draw_picker_page(PAGE_SCENE,  true);
            else                          scroll_to_page(PAGE_PLAY);
            return;
        }

        voices_seen = 0;
        uint32_t keycol = key_col();
        /* play surface, now with an activity glow/sparkle via the brightness cb */
        /* 4-voice polyphony: the synth renders inside the same core1 2ms budget
         * as our FX; 8 voices' render time pushed us over. 4 leaves headroom for
         * the full chain and is plenty for an ambient wash. */
        /* Quartal, always-in-key play surface: 8 strings tuned in DIATONIC 4THS
           (interval_degrees = 3 scale steps) within the selected key + mode, so every
           adjacent cluster is an open, in-key chord and sliding up a string walks the
           scale. Root register follows the circle-of-fifths key (KEY pads); the mode (BANK
           pads) picks the scale, so the whole surface + the Spectra wash share one tonal world.
           Scale bitmask, bit N = N semitones above the root. */
        static const uint16_t kModeScale[5] = {
            2741,   /* Ionian / major    0,2,4,5,7,9,11 */
            1453,   /* Aeolian / minor   0,2,3,5,7,8,10 */
            1709,   /* Dorian            0,2,3,5,7,9,10 */
            2773,   /* Lydian            0,2,4,6,7,9,11 */
            1717,   /* Mixolydian        0,2,4,5,7,9,10 */
        };
        int msel = (mode_sel < 0 || mode_sel > 4) ? 0 : mode_sel;
        play.do_play_surface(0, UI_Y, 8, UI_H, 4, DIMMEST(keycol), keycol, 48 + fx.key, 3, note_cb, this,
                             VERTICAL | SHOW_BACKGROUND | STRINGOPHONIC_MONO, kModeScale[msel], fx.key,
                             viz_brightness, this);
        unsigned short released = (unsigned short)(voices_active & ~voices_seen);
        for (int v = 0; v < 16; v++) if (released & (1u << v)) synth_note_up(v);
        voices_active = voices_seen;
        static const char* nm[FX_N] = { "Orbit","Satellite","Constellate","Tail","Flux","Spectra","Mix" };
        /* Tail + Flux flash IN SYNC on one LFO whose rate tracks the Flux mod rate — the
           modulation pair, at a speed distinct from the loop meters. */
        float tail = fx_val[FX_TAIL] / 127.f, flux = fx_val[FX_FLUX] / 127.f;
        shimmer_phase += (float)dt_us * 1e-6f * (0.35f + fx.mod_rate * 2.2f); shimmer_phase -= (float)(int)shimmer_phase;
        float shTri   = shimmer_phase < 0.5f ? shimmer_phase * 2.f : 2.f - shimmer_phase * 2.f;
        float shimmer = shTri * shTri * (3.f - 2.f * shTri);
        freeze_phase += (float)dt_us * 1e-6f * 2.5f; freeze_phase -= (float)(int)freeze_phase;   /* Satellite freeze bounce */
        for (int i = 0; i < FX_N; i++) {
            /* Reactive sliders pulse their colour; Spectra & Mix stay steady.
               (the *N.f gains below are the obvious brightness tuning knobs) */
            int bri = 256;
            switch (i) {
                case FX_ORBIT:       bri = meter_bri(viz_loop_env,  viz_loop_pk,  0.02f); break;  /* pulse: main loop emitting */
                case FX_SATELLITE:   bri = meter_bri(viz_micro_env, viz_micro_pk, 0.01f); break;  /* pulse: micro-loop emitting */
                case FX_CONSTELLATE: bri = meter_bri(viz_grain_env, viz_grain_pk, 0.01f); break;  /* pulse: grains firing */
                case FX_TAIL:        bri = 60 + (int)(tail * shimmer * 196.f); break;  /* Tail + Flux shimmer in sync */
                case FX_FLUX:        bri = 60 + (int)(flux * shimmer * 196.f); break;
            }
            if (bri < 0) bri = 0; if (bri > 256) bri = 256;
            fxslider[i].simple_slider(8 + i, UI_Y, UI_H, VERTICAL | SHOW_STEM,
                                      fade_col(palette[8][i], bri), 0, 127, fx_val[i], nm[i]);
            fx_val[i] = (unsigned char)last_widget_new_value();

            /* Orbit & Satellite each carry a white "star" that falls down its own column
               once per loop cycle — a per-column clock. When Satellite is at FREEZE the
               micro-loop isn't cycling, so the star stops falling and instead bounces
               fast up/down between three rows (held in place) to read as "frozen". */
            if (i == FX_SATELLITE && fx.micro_hold >= 0.9f) {
                int step = (int)(freeze_phase * 4.f) & 3;                 /* 0..3 */
                int off  = (step == 1) ? -1 : (step == 3) ? 1 : 0;        /* centre, up, centre, down */
                set_led(8 + i, UI_MID_UP + off, fade_col(WHITE, 240));
            } else if (i == FX_ORBIT || i == FX_SATELLITE) {
                unsigned int phase = (i == FX_ORBIT) ? viz_loop     : viz_micro;
                unsigned int len   = (i == FX_ORBIT) ? viz_loop_len : viz_micro_len;
                int row = len ? UI_Y + (int)(((float)phase / (float)len) * (float)(UI_H - 1)) : UI_Y;
                if (row < UI_Y) row = UI_Y; if (row > UI_Y1) row = UI_Y1;
                int sb = 130 + (bri - 36) / 2;  if (sb > 256) sb = 256;
                set_led(8 + i, row, fade_col(WHITE, sb));
            }

        }
        push_fx_from_ui();

        /* col-15: bipolar Gravity (up) / Event Horizon (down). Neutral = centre. Call
           simple_slider for the touch/value, then draw our own bipolar column over it. */
        int gd = (int)fx_val15 - 64;
        fxslider15.simple_slider(15, UI_Y, UI_H, VERTICAL | SHOW_STEM,
                                 fade_col(gd >= 0 ? GREEN : RED, 256), 0, 127, fx_val15, "Grav/Drain");
        fx_val15 = (unsigned char)last_widget_new_value();
        gd = (int)fx_val15 - 64;
        /* Deadzone around centre: a band near the middle reads as neutral (white, no
           Gravity/drain) so you don't have to land exactly on centre. geff = how far past
           the deadzone you are; 0 inside it. */
        const int DZ = 10;
        int geff = (gd > DZ) ? (gd - DZ) : (gd < -DZ ? (gd + DZ) : 0);
        /* Bipolar fill that GROWS from the centre (the middle of the usable band):
           GREEN up (Gravity), RED down (Event Horizon). All lit pads coloured; the two
           middle LEDs show WHITE only inside the neutral deadzone. */
        for (int y = UI_Y; y <= UI_Y1; y++) {
            uint32_t c = 0;                                       /* off */
            if (geff == 0) {
                if (y == UI_MID_UP || y == UI_MID_DN) c = LED_RGB(20, 20, 20);   /* neutral marker */
            } else if (geff > 0) {                               /* Gravity: green fill, centre -> top */
                int topRow = UI_MID_UP - (geff * UI_HALF) / (63 - DZ);
                if (y >= topRow && y <= UI_MID_UP) c = fade_col(GREEN, 256);
            } else {                                             /* drain: red fill, centre -> bottom */
                int botRow = UI_MID_DN + ((-geff) * UI_HALF) / (64 - DZ);
                if (y >= UI_MID_DN && y <= botRow) c = fade_col(RED, 256);
            }
            set_led(15, y, c);
        }
        fx.gravity = geff > 0 ? (float)geff / (float)(63 - DZ) : 0.f;              /* up   -> gravity 0..1 */
        fx.horizon = geff < 0 ? 1.f - (float)(-geff) / (float)(64 - DZ) : 1.f;     /* down -> horizon 1..0 */

        draw_control_rows();
    }

    /* Core-1 audio hook (new API). Base renders the synth into mix_buffers_out;
     * we run Ambiotica on its dry stereo bus, write final int16 to audiobuf_out,
     * and return true to own the output (bypass the built-in FX — Ambiotica IS
     * the FX). Running on core1 means an overrun crackles rather than locking the
     * UI/USB, so this is where the heavy chain belongs.
     *
     * v1: dry bus only. The synth's reverb/delay SEND buses are not captured yet
     * (their half-rate format needs confirming); if a preset routes signal to
     * them it'll sound thinner. v1.1 will route reverbsend -> reverb input and
     * delaysend -> micro-loop input. */
    bool on_dsp(const int16_t* audiobuf_in, int16_t* audiobuf_out,
                mix_buffers_t* mix_buffers_out) override {
        panel_t::on_dsp(audiobuf_in, audiobuf_out, mix_buffers_out);   /* render synth -> dry */

        if (!dsp_ok) {                                   /* passthrough (alloc failed) */
            for (int i = 0; i < BLOCK_SIZE * 2; i++) {
                int v = mix_buffers_out->dry[i];
                audiobuf_out[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
            }
            return true;
        }

        /* Preset auditions must be heard CLEAN. The picker previews by playing the built-in
           synth, so it lands in the same dry bus we swallow — there is no separate preview
           bus to route around. Left alone it is not just soaked in the wash: the looper
           CAPTURES it, so browsing a bank smears audition blips through the pad for minutes.
           So while the audition sounds, gate the chain's INPUT (it hears silence and its tail
           keeps decaying underneath) and add the dry synth straight to the output below.
           preview_pressure is the picker's own "audition is making sound" byte; core1 reading
           it a block late is harmless. Fast in so the attack isn't soaked, slow out so the
           wash returns gracefully rather than snapping back. */
        float pv_target = presets.picker.preview_pressure ? 1.0f : 0.0f;
        preview_mix += (pv_target - preview_mix) * (pv_target > preview_mix ? 0.5f : 0.08f);
        const float duck = 1.0f - preview_mix;

        const float k = (1.0f / 32768.0f) * AMB_IN_GAIN * duck;   /* headroom for polyphony */
        for (int i = 0; i < BLOCK_SIZE; i++) {
            sL[i] = mix_buffers_out->dry[2*i]   * k;
            sR[i] = mix_buffers_out->dry[2*i+1] * k;
        }
        /* External input (settings page: src = line / mic, in = level). Summed into the same
           bus as the synth so the whole chain — looper, grains, plate, Spectra — processes it.
           Default src is "off", which is the historical synth-only behaviour. Watch the level
           on "mic": the chain feeds back through the looper, so a hot mic can run away. */
        if (audio_source && audiobuf_in) {
            const float ki = (1.0f / 32768.0f) * AMB_EXT_IN_GAIN * (audio_in_level / 127.0f) * duck;
            for (int i = 0; i < BLOCK_SIZE; i++) {
                sL[i] += audiobuf_in[2*i]   * ki;
                sR[i] += audiobuf_in[2*i+1] * ki;
            }
        }

        /* Per-block macro smoothing (~30 ms). The plugin gets smooth param values
           from the host; the Plinky sliders deliver coarse 0..127 steps, so pushing
           them straight to the chain clicks (Tail room-size, Spectra amount, micro
           hold, ...). Ramp the continuous macros toward their targets; length +
           discrete params pass through (their DSP crossfades/snaps handle those). */
        const float ps = 0.06f;
        /* Ramp toward the target, then SNAP when within a deadband so fx_sm settles
           bit-exactly on fx — otherwise the one-pole asymptote never equals the
           target and fc_push_params' memcmp gate would re-run powf/expf every block
           (the DSP budget is tight). During an active move it re-pushes as intended. */
        #define AMB_SM(f) { float d_ = fx.f - fx_sm.f; fx_sm.f += ps * d_; \
                            if (d_ < 5e-4f && d_ > -5e-4f) fx_sm.f = fx.f; }
        AMB_SM(mix); AMB_SM(loop_layer); AMB_SM(grain_size); AMB_SM(scatter);
        AMB_SM(micro_hold); AMB_SM(decay); AMB_SM(mod_depth); AMB_SM(mod_rate);
        AMB_SM(bloom); AMB_SM(drift_amt); AMB_SM(spectra); AMB_SM(ring);
        AMB_SM(gravity); AMB_SM(horizon);
        #undef AMB_SM
        fx_sm.loop_length_bars = fx.loop_length_bars; fx_sm.micro_bars = fx.micro_bars;
        fx_sm.bpm = fx.bpm; fx_sm.key = fx.key; fx_sm.chord = fx.chord;

        /* Event Horizon: at the BOTTOM of col-15 the drain ends in a one-shot flush that
           clears the content buffers, so bringing the slider back up starts empty.
           Gate on the SMOOTHED horizon, not the raw tap: by the time it settles to the
           bottom the drain has already silenced the loop and micro-loop (micro hold has
           reached 0), so the flush clears quiet buffers. Flushing on the raw tap instead
           cleared them mid-drain, into a still-live signal that instantly refilled — the
           short frozen-loop "buzz". Hysteresis so it can't re-fire on the edge; the
           reverb is left out so its tail decays gracefully rather than being cut. */
        if (fx_sm.horizon < 0.04f) {
            if (!eh_flushed) {
                if (looper)    looper_reset(looper);
                if (microloop) microloop_reset(microloop);
                if (granular)  granular_reset(granular);
                if (harmony)   harmony_reset(harmony);
                if (bloom)     bloom_reset(bloom);
                if (drift)     drift_reset(drift);
                eh_flushed = true;
            }
        } else if (fx_sm.horizon > 0.10f) {
            eh_flushed = false;
        }

        fc_render_block(&st, looper, granular, microloop, harmony, bloom, drift,
                        &fx_sm, AMB_SR, sL, sR, oL, oR, BLOCK_SIZE);

        /* Drums go in HERE — into the chain's output, after the wash, never into sL/sR.
           That is the whole point: the looper, the grains and the plate never see them, so
           the pattern stays dry and legible while the wash does whatever it likes behind it.
           It also means drums cost no polyphony: they are our own playback, not synth voices. */
#ifdef AMB_PROFILE
        unsigned int _td = time_us();
#endif
        drums_render(drums, oL, oR, BLOCK_SIZE);
#ifdef AMB_PROFILE
        g_stage_us[7] += time_us() - _td;
#endif

        for (int i = 0; i < BLOCK_SIZE; i++) {
            int l = (int)(oL[i] * 32767.0f), r = (int)(oR[i] * 32767.0f);
            if (preview_mix > 0.001f) {   /* audition, dry and unprocessed, over the decaying tail */
                l += (int)(mix_buffers_out->dry[2*i]   * preview_mix);
                r += (int)(mix_buffers_out->dry[2*i+1] * preview_mix);
            }
            audiobuf_out[2*i]   = (int16_t)(l < -32768 ? -32768 : l > 32767 ? 32767 : l);
            audiobuf_out[2*i+1] = (int16_t)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
        }

        /* cheap visualization taps: output level (breathing) + per-channel block
           peaks for the main loop, micro-loop and granular. */
        float op = 0.f, gp = 0.f, lp = 0.f, mp = 0.f;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            float a = oL[i]       < 0.f ? -oL[i]       : oL[i];       if (a > op) op = a;
            float g = st.granL[i] < 0.f ? -st.granL[i] : st.granL[i]; if (g > gp) gp = g;
            float l = st.loopL[i] < 0.f ? -st.loopL[i] : st.loopL[i]; if (l > lp) lp = l;
            float m = st.micL[i]  < 0.f ? -st.micL[i]  : st.micL[i];  if (m > mp) mp = m;
        }
        viz_out       = viz_out * 0.9f + op * 0.1f;
        /* per-channel meter: fast envelope + slow-release peak-hold (see meter_bri) */
        viz_loop_env  = viz_loop_env  * 0.90f + lp * 0.10f;  viz_loop_pk  = lp > viz_loop_pk  ? lp : viz_loop_pk  * 0.9990f;
        viz_micro_env = viz_micro_env * 0.90f + mp * 0.10f;  viz_micro_pk = mp > viz_micro_pk ? mp : viz_micro_pk * 0.9990f;
        viz_grain_env = viz_grain_env * 0.90f + gp * 0.10f;  viz_grain_pk = gp > viz_grain_pk ? gp : viz_grain_pk * 0.9990f;
        int llen = (int)(fx.loop_length_bars * 4.f * 60.f / (fx.bpm > 0.f ? fx.bpm : 120.f) * (float)AMB_SR);
        if (llen < 1) llen = 1;
        viz_loop_len = (unsigned)llen;
        viz_loop += BLOCK_SIZE; if (viz_loop >= (unsigned)llen) viz_loop -= (unsigned)llen;
        int mlen = (int)(fx.micro_bars * 4.f * 60.f / (fx.bpm > 0.f ? fx.bpm : 120.f) * (float)AMB_SR);
        if (mlen < 1) mlen = 1;
        viz_micro_len = (unsigned)mlen;
        /* Freeze the micro-loop star when Satellite is at/near the top (held shimmer). */
        if (fx.micro_hold < 0.9f) { viz_micro += BLOCK_SIZE; if (viz_micro >= (unsigned)mlen) viz_micro -= (unsigned)mlen; }
        return true;
    }

    /* No on_click override: key and mode now live on the printed KEY / BANK pads, so the
       physical side buttons keep their stock behaviour (left = BPM, right = page cycle).
       The right pair is therefore a second way between pages, on top of SYNTH/PRESET/SCALE. */

    /* The scene: everything the SONG page save/loads per slot. Macro positions, the tuning,
       and the synth + mix presets, so a loaded slot sounds exactly like it was saved.
       Derived state is rebuilt after a read — fx is a function of the raw slider bytes, and
       fx_sm is snapped to it so nothing audibly ramps in from the old scene. */
    bool on_serialise(serialiser_t& s, int version) override {
        (void)version;
        ambiotica& o = *this;
        /* FIELD_BASE64 takes its length by non-const reference (it writes back how much was
           actually read), so this needs an lvalue, declared before OBJECT_BEGIN opens its
           switch. Same shape the stock worm panel uses. */
        int pattern_bytes = (int)sizeof(o.pattern);
        int mod_bytes     = (int)sizeof(o.pattern_mod);
        OBJECT_BEGIN(s);
        FIELD("orbit",   o.fx_val[FX_ORBIT],       0u, 127u);
        FIELD("satel",   o.fx_val[FX_SATELLITE],   0u, 127u);
        FIELD("constel", o.fx_val[FX_CONSTELLATE], 0u, 127u);
        FIELD("tail",    o.fx_val[FX_TAIL],        0u, 127u);
        FIELD("flux",    o.fx_val[FX_FLUX],        0u, 127u);
        FIELD("spectra", o.fx_val[FX_SPECTRA],     0u, 127u);
        FIELD("mix",     o.fx_val[FX_MIX],         0u, 127u);
        FIELD("gravity", o.fx_val15,               0u, 127u);
        FIELD("key",     o.key_pos,                0,  11);
        FIELD("mode",    o.mode_sel,               0,  4);
        FIELD_BASE64("drums", o.pattern, pattern_bytes, (int)sizeof(o.pattern));
        /* Separate field, so a scene written before modulo existed just omits it and loads
           with no conditions set — which is exactly how it used to sound. */
        FIELD_BASE64("dmod",  o.pattern_mod, mod_bytes, (int)sizeof(o.pattern_mod));
        FIELD("kit",     o.drum_kit,               -1, 8);
        FIELD("dmute",   o.drum_mute,              0u, 255u);
        FIELD("dtrans", o.drum_transpose,        -24, 24);
        /* Tested 2026-07-25: removing these does NOT stop the system running its "plantime"
         * preset-install planner on a scene load — that happens for every staged panel load
         * regardless of what we serialise. So they are not implicated in the load failure,
         * and a scene should carry its sound. */
        FIELD_SYNTH_PRESET("preset", 0);
        FIELD_MIX_PRESET("presetMix");
        OBJECT_END(s);
        if (s.reading) { apply_key_mode(); push_fx_from_ui(); fx_sm = fx; }
        return true;
    }

    /* Panel PREFERENCES — not part of a scene. These follow the box, not the slot, and the
       system reloads them automatically at boot. */
    bool on_serialise_settings(serialiser_t& s, int version) override {
        (void)version;
        ambiotica& o = *this;
        OBJECT_BEGIN(s);
        FIELD("audio_source", o.audio_source,   0u, 2u);
        FIELD("audio_in",     o.audio_in_level, 0u, 127u);
        OBJECT_END(s);
        return true;
    }
};
