/* Ambiotica-on-Plinky — panel.
 *
 * Play surface (left half) drives the built-in synth; its dry bus feeds the Ambiotica
 * chain (full_chain.h), which runs on core1 in on_dsp() and owns the output (the
 * built-in FX are silenced — Ambiotica IS the FX). See PORT_NOTES.md for the
 * core1-budget story. Build config: amb_config.h (LOOPER_I16) concatenated by
 * amalgamate.sh; per-module flags live in their own files (shimmer/cloud off in
 * microloop.c). Reverb is always the Dattorro plate.
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

struct ambiotica_panel : panel_t {
    looper_t* looper = 0; granular_t* granular = 0; microloop_t* microloop = 0;
    harmony_t* harmony = 0; bloom_t* bloom = 0; drift_t* drift = 0;
    bool dsp_ok = false;

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
        push_fx_from_ui();
        fx_sm = fx;   /* start the smoother at the target so nothing ramps up from 0 on boot */

        /* Ambiotica IS the FX and owns the output (on_dsp returns true), so do_fx never
         * runs — but zero the synth voices' reverb/delay sends defensively so nothing can
         * bleed into a native bus. */
        set_param_packed(VOICE_PARAM_REVERB_SEND, 0, &synth_presets[synth_preset]);
        set_param_packed(VOICE_PARAM_DELAY_SEND,  0, &synth_presets[synth_preset]);

        g_amb_ps_base = get_psram_ptr(); g_amb_ps_cap = get_psram_size(); g_amb_ps_used = 0;
        g_amb_sr_base = sram_pool;       g_amb_sr_cap = sizeof(sram_pool);  g_amb_sr_used = 0;
        const int sr = (int) AMB_SR;
        const int loopcap = 32 * sr;
        const bool ps = g_amb_ps_cap >= (size_t) 4 * 1024 * 1024;   /* looper 4 MB (+ big modules at high levels) */
        g_amb_region = 1;   /* PSRAM: big buffers (looper/micro/granular; sequential-ish access) */
        if (ps) looper    = looper_create(loopcap, sr);
        if (ps) microloop = microloop_create(sr);
        if (ps) granular  = granular_create(sr);
        g_amb_region = 0;   /* SRAM pool: fast-access modules (dattorro/harmony/drift/bloom) */
        bloom   = bloom_create(sr);
        drift   = drift_create(sr);
        harmony = harmony_create(sr);
        dsp_ok = looper && microloop && granular && bloom && drift && harmony;
        fc_init(&st, 0.7f);
        st.dat = dattorro_create(sr);   /* Dattorro plate (SRAM region); after fc_init zeroes st */
        dsp_ok = dsp_ok && st.dat;
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
        ambiotica_panel* self = (ambiotica_panel*)user;
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
        ambiotica_panel* self = (ambiotica_panel*)user;
        int b = (int)(self->viz_out * 28.f);                       /* breathing base glow */
        if (b < 0) b = 0; if (b > 255) b = 255;
        return (unsigned char)b;
    }

    void on_ui(int dt_us) override {
        leds_clear();
#ifdef AMB_PROFILE
        /* Per-stage core1 timing (avg us/block). Prints UNCONDITIONALLY ~8x/sec so it's a
         * heartbeat too: if this never appears, printf/on_ui isn't reaching the console;
         * if it shows (n=0) the DSP block isn't completing; otherwise it's the breakdown. */
        static unsigned prof_ctr = 0;
        if ((++prof_ctr % 30) == 0) {
            unsigned nn = g_stage_n ? g_stage_n : 1;
            printf("STG loop=%u gran=%u mic=%u rev=%u harm=%u mix=%u push=%u  (n=%u)\n",
                   g_stage_us[0]/nn, g_stage_us[1]/nn, g_stage_us[2]/nn, g_stage_us[3]/nn,
                   g_stage_us[4]/nn, g_stage_us[5]/nn, g_stage_us[6]/nn, g_stage_n);
            for (int s = 0; s < 7; s++) g_stage_us[s] = 0;
            g_stage_n = 0;
        }
#endif
        voices_seen = 0;
        /* Play-surface colour: hue = key (root), shade/brightness = mode — so the
           left/right buttons visibly recolour the surface as they move the key around
           the circle of fifths / change the mode. */
        /* Play-surface hue is reserved to 8..15 so it can never collide with a macro
           slider (those use hues 0..6, Orbit = 0) — the two halves must stay tellable
           apart. Brightness still tracks the chord, hue still tracks the key. */
        uint32_t keycol = palette[(7 + mode_sel * 2) & 15][8 + (fx.key & 7)];
        /* play surface, now with an activity glow/sparkle via the brightness cb */
        /* 4-voice polyphony: the synth renders inside the same core1 2ms budget
         * as our FX; 8 voices' render time pushed us over. 4 leaves headroom for
         * the full chain and is plenty for an ambient wash. */
        /* Quartal, always-in-key play surface: 8 strings tuned in DIATONIC 4THS
           (interval_degrees = 3 scale steps) within the selected key + mode, so every
           adjacent cluster is an open, in-key chord and sliding up a string walks the
           scale. Root register follows the circle-of-fifths key; the mode (right buttons)
           picks the scale, so the whole surface + the Spectra wash share one tonal world.
           Scale bitmask, bit N = N semitones above the root. */
        static const uint16_t kModeScale[5] = {
            2741,   /* Ionian / major    0,2,4,5,7,9,11 */
            1453,   /* Aeolian / minor   0,2,3,5,7,8,10 */
            1709,   /* Dorian            0,2,3,5,7,9,10 */
            2773,   /* Lydian            0,2,4,6,7,9,11 */
            1717,   /* Mixolydian        0,2,4,5,7,9,10 */
        };
        int msel = (mode_sel < 0 || mode_sel > 4) ? 0 : mode_sel;
        play.do_play_surface(0, 0, 8, 16, 4, DIMMEST(keycol), keycol, 48 + fx.key, 3, note_cb, this,
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
            fxslider[i].simple_slider(8 + i, 0, 16, VERTICAL | SHOW_STEM,
                                      fade_col(palette[8][i], bri), 0, 127, fx_val[i], nm[i]);
            fx_val[i] = (unsigned char)last_widget_new_value();

            /* Orbit & Satellite each carry a white "star" that falls down its own column
               once per loop cycle — a per-column clock. When Satellite is at FREEZE the
               micro-loop isn't cycling, so the star stops falling and instead bounces
               fast up/down between three rows (held in place) to read as "frozen". */
            if (i == FX_SATELLITE && fx.micro_hold >= 0.9f) {
                int step = (int)(freeze_phase * 4.f) & 3;                 /* 0..3 */
                int off  = (step == 1) ? -1 : (step == 3) ? 1 : 0;        /* centre, up, centre, down */
                set_led(8 + i, 7 + off, fade_col(WHITE, 240));
            } else if (i == FX_ORBIT || i == FX_SATELLITE) {
                unsigned int phase = (i == FX_ORBIT) ? viz_loop     : viz_micro;
                unsigned int len   = (i == FX_ORBIT) ? viz_loop_len : viz_micro_len;
                int row = len ? (int)(((float)phase / (float)len) * 15.f) : 0;
                if (row < 0) row = 0; if (row > 15) row = 15;
                int sb = 130 + (bri - 36) / 2;  if (sb > 256) sb = 256;
                set_led(8 + i, row, fade_col(WHITE, sb));
            }

        }
        push_fx_from_ui();

        /* col-15: bipolar Gravity (up) / Event Horizon (down). Neutral = centre. Call
           simple_slider for the touch/value, then draw our own bipolar column over it. */
        int gd = (int)fx_val15 - 64;
        fxslider15.simple_slider(15, 0, 16, VERTICAL | SHOW_STEM,
                                 fade_col(gd >= 0 ? GREEN : RED, 256), 0, 127, fx_val15, "Grav/Drain");
        fx_val15 = (unsigned char)last_widget_new_value();
        gd = (int)fx_val15 - 64;
        /* The middle two LEDs (rows 7,8 — the true centre of a 0..15 column) stay WHITE so
           neutral is always visible. The WHOLE column tints from white toward GREEN as it's
           scrolled up (Gravity) / RED as it's scrolled down (Event Horizon); the tint depth
           tracks how far from centre. */
        float amt = (gd >= 0) ? (float)gd / 63.f : (float)(-gd) / 64.f;   /* 0..1 magnitude */
        if (amt > 1.f) amt = 1.f;
        for (int y = 0; y < 16; y++) {
            int r, g, b;
            if (y == 7 || y == 8) { r = g = b = 24; }                                              /* white centre */
            else if (gd > 0) { r = (int)(18.f - 18.f*amt); g = (int)(18.f + 13.f*amt); b = (int)(18.f - 18.f*amt); }  /* white->green */
            else if (gd < 0) { r = (int)(18.f + 13.f*amt); g = (int)(18.f - 18.f*amt); b = (int)(18.f - 18.f*amt); }  /* white->red */
            else             { r = g = b = 18; }                                                   /* neutral white */
            set_led(15, y, LED_RGB(r, g, b));
        }
        fx.gravity = gd > 0 ? (float)gd / 63.f : 0.f;          /* up   -> gravity 0..1 */
        fx.horizon = gd < 0 ? (float)fx_val15 / 64.f : 1.f;    /* down -> horizon 1..0 (drain) */
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

        const float k = (1.0f / 32768.0f) * AMB_IN_GAIN;   /* headroom for polyphony */
        for (int i = 0; i < BLOCK_SIZE; i++) {
            sL[i] = mix_buffers_out->dry[2*i]   * k;
            sR[i] = mix_buffers_out->dry[2*i+1] * k;
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

        for (int i = 0; i < BLOCK_SIZE; i++) {
            int l = (int)(oL[i] * 32767.0f), r = (int)(oR[i] * 32767.0f);
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

    /* Physical side buttons: LEFT pair steps the KEY around the circle of fifths
       (+1 = up a fifth = +7 semitones); RIGHT pair steps the MODE (Ionian/Aeolian/
       Dorian/Lydian/Mixolydian), which sets the surface scale AND the Spectra wash
       tonic. The play-surface colour follows (hue = key, shade = mode). Unhandled
       buttons fall through to the system (BPM / pages). */
    void on_click(uint8_t button_mask) override {
        bool handled = false;
        if ((button_mask & BTN_BIT_LUP)   && button_clicked(BUTTON_LUP))   { key_pos    = (key_pos + 1)  % 12; handled = true; }
        if ((button_mask & BTN_BIT_LDOWN) && button_clicked(BUTTON_LDOWN)) { key_pos    = (key_pos + 11) % 12; handled = true; }
        if ((button_mask & BTN_BIT_RUP)   && button_clicked(BUTTON_RUP))   { mode_sel = (mode_sel + 1) % 5; handled = true; }
        if ((button_mask & BTN_BIT_RDOWN) && button_clicked(BUTTON_RDOWN)) { mode_sel = (mode_sel + 4) % 5; handled = true; }
        if (handled) {
            fx.key   = (key_pos * 7) % 12;   /* circle of fifths -> root semitone offset */
            fx.chord = mode_sel;
        } else {
            panel_t::on_click(button_mask);
        }
    }

    bool on_serialise(serialiser_t& s, int version) override {
        /* TODO: persist fx_val[], synth_preset, fx.key/chord via save_and_load.h field macros */
        return panel_t::on_serialise(s, version);
    }
};
