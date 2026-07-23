/* Ambiotica-on-Plinky — panel.
 *
 * Built-in synth (play surface, left half) -> Ambiotica chain -> audio out.
 * The chain runs on core1 in on_dsp() and owns the output (bypasses the built-in
 * FX; Ambiotica IS the FX). See PORT_NOTES.md for the core1-budget story.
 *
 * Compile-time knobs:
 *   AMB_CHAIN_LEVEL 1..6  chain-complexity ladder (default 6 = full); a perf tool
 *   AMB_DEBUG             per-stage peak/HF probe, printed from on_ui (core0)
 *   AMB_BYPASS           skip the chain (synth -> out) to isolate FX vs I/O
 */
#define PANEL_PAD_COLOR TEAL
#define AMB_SR 32000.0
#define FC_SOFT_CLIP            /* soft-limit the chain output (see full_chain.h) */
/* Attenuate the Plinky synth before the chain: polyphonic voices sum hotter than
 * the plugin's single input, so headroom avoids slamming the ceiling. The WHOLE
 * chain (loop x1.9 makeup, drift regen, layer sums) was tuned in the plugin around
 * a host-nominal ~0.18 peak input (see ambiotica-plugin tools/buildup_test.cpp);
 * at 0.35 the raw poly bus still lands well above that and pins the limiter. The
 * REVLVL 'in=' readout (chain-input peak x1000) tells us where we sit vs ~180. */
#define AMB_IN_GAIN 0.12f

enum { FX_ORBIT, FX_CONSTELLATE, FX_SATELLITE, FX_TAIL, FX_FLUX, FX_SPECTRA, FX_MIX, FX_N };

struct ambiotica_panel : panel_t {
    looper_t* looper = 0; granular_t* granular = 0; microloop_t* microloop = 0; reverb_t* reverb = 0;
    harmony_t* harmony = 0; bloom_t* bloom = 0; drift_t* drift = 0;
    bool dsp_ok = false;

    full_params fx;      /* target macros (set from the sliders in on_ui) */
    full_params fx_sm;   /* per-block-smoothed macros actually fed to the chain (de-click) */
    fc_state    st;
    /* Reverb + harmony + drift + bloom live here (fast SRAM) — the reverb's
     * scattered delay-line access was too slow in PSRAM. ~82 KB used; fits the
     * 128 KB panel arena. Looper/micro-loop/granular stay in PSRAM. */
    unsigned char sram_pool[88 * 1024];

    float sL[BLOCK_SIZE], sR[BLOCK_SIZE], oL[BLOCK_SIZE], oR[BLOCK_SIZE];
    play_surface_t play;
    slider_t       fxslider[FX_N];
    unsigned char  fx_val[FX_N];
    int            synth_preset = 0;
    unsigned short voices_active = 0, voices_seen = 0;
    int            reverb_warmup = 0;   /* native-reverb: do_fx warmup blocks done */

    /* visualization taps — written in on_dsp (core1 audio), read in on_ui.
       Each reactive slider is a SELF-CALIBRATING meter: env = fast envelope,
       pk = slow-release peak-hold. on_ui maps env/pk -> brightness (meter_bri),
       so every column pulses across the full brightness range regardless of the
       absolute signal level (loopL, micL and granL differ ~10x in magnitude). */
    float        viz_out = 0.f;                                 /* output level (play-surface breathing) */
    float        viz_outpk = 0.f;                               /* DEBUG: final-output peak-hold */
    float        viz_inpk = 0.f;                                /* DEBUG: chain-input peak-hold (vs ~0.18 nominal) */
    float        viz_loop_env = 0.f,  viz_loop_pk = 0.f;       /* Orbit: main-loop emit meter */
    float        viz_micro_env = 0.f, viz_micro_pk = 0.f;      /* Satellite: micro-loop emit meter */
    float        viz_grain_env = 0.f, viz_grain_pk = 0.f;      /* Constellate: granular meter */
    unsigned int viz_loop = 0, viz_micro = 0, frame_ctr = 0;   /* main-loop & micro-loop phase counters, UI frame */
    unsigned int viz_loop_len = 1, viz_micro_len = 1;          /* their cycle lengths in samples (falling-star clocks) */
    float        shimmer_phase = 0.f;              /* UI-side shimmer LFO shared by the Tail+Flux sliders */

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

#ifdef AMB_DEBUG
    /* Per-stage metrics computed on core1 (on_dsp), printed on core0 (on_ui) —
     * printf only reaches the WebUSB debug pane from core0. */
    volatile int dbg_pk[6], dbg_hf[6];
    void dbg_measure(int idx, const float* b) {
        float pk = 0.f, sq = 0.f, dif = 0.f;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            float x = b[i]; float a = x < 0 ? -x : x; if (a > pk) pk = a;
            sq += x * x;
            if (i) { float dd = b[i] - b[i-1]; dif += dd * dd; }
        }
        dbg_pk[idx] = (int)(pk * 1000.f);
        dbg_hf[idx] = (int)(1000.f * dif / (sq + 1e-9f));   /* HF metric x1000 */
    }
#endif

    void setup_default_panel_state() override {
        fx_val[FX_ORBIT] = 48; fx_val[FX_CONSTELLATE] = 48; fx_val[FX_SATELLITE] = 32;
        fx_val[FX_TAIL] = 76; fx_val[FX_FLUX] = 40; fx_val[FX_SPECTRA] = 64; fx_val[FX_MIX] = 90;
        memset(&fx, 0, sizeof fx);
        fx.bpm = 120.f; fx.loop_length_bars = 2.f; fx.key = 0; fx.chord = 0; fx.bloom = 0.4f;
        push_fx_from_ui();
        fx_sm = fx;   /* start the smoother at the target so nothing ramps up from 0 on boot */

        /* Ambiotica IS the FX. Silence the Plinky built-in reverb/delay so it can't
         * run in parallel with our chain: (1) the stock synth preset ships
         * reverb_send=24, and (2) the MIX preset's REVERB_SHIMMER/FEEDBACK drive
         * the native reverb do_reverb() reads — the octave-shimmer cascade.
         * set_param_packed(...,0,...) clears every corner. (Mix-param addressing:
         * raw MIX_PARAM_* — if it needs +128, this is the one line to flip.) */
        set_param_packed(VOICE_PARAM_REVERB_SEND,   0, &synth_presets[synth_preset]);
        set_param_packed(VOICE_PARAM_DELAY_SEND,    0, &synth_presets[synth_preset]);
        set_param_packed(MIX_PARAM_REVERB_SHIMMER,  0, &synth_presets[MIX_PRESET_IDX]);   /* shimmer OFF */
        /* Gentle fixed feedback for a decaying ambient tail (packed = value in each of
         * the 8 corner bytes). Low: it builds up + adds to the mix. TUNABLE. */
        set_param_packed(MIX_PARAM_REVERB_FEEDBACK, (unsigned long long)12 * 0x0101010101010101ULL, &synth_presets[MIX_PRESET_IDX]);

        g_amb_ps_base = get_psram_ptr(); g_amb_ps_cap = get_psram_size(); g_amb_ps_used = 0;
        g_amb_sr_base = sram_pool;       g_amb_sr_cap = sizeof(sram_pool);  g_amb_sr_used = 0;
        const int sr = (int) AMB_SR;
        const int loopcap = 32 * sr;
        const bool ps = g_amb_ps_cap >= (size_t) 4 * 1024 * 1024;   /* looper 4 MB (+ big modules at high levels) */
        g_amb_region = 1;   /* PSRAM: big buffers (sequential-ish) */
#if AMB_CHAIN_LEVEL >= 1
        if (ps) looper    = looper_create(loopcap, sr);
#endif
#if AMB_CHAIN_LEVEL >= 5
        if (ps) microloop = microloop_create(sr);
#endif
#if AMB_CHAIN_LEVEL >= 6
        if (ps) granular  = granular_create(sr);
#endif
        g_amb_region = 0;   /* SRAM pool: fast-access modules */
#if (AMB_CHAIN_LEVEL >= 2) && !defined(AMB_BUILTIN_REVERB)
        reverb  = reverb_create(sr);        /* modal reverb (SRAM); skipped when AMB_BUILTIN_REVERB uses do_reverb */
#endif
#if AMB_CHAIN_LEVEL >= 3
        bloom   = bloom_create(sr);
        drift   = drift_create(sr);
#endif
#if AMB_CHAIN_LEVEL >= 4
        harmony = harmony_create(sr);
#endif
        /* dsp_ok = every module this level will process was created */
        dsp_ok = true;
#if AMB_CHAIN_LEVEL >= 1
        dsp_ok = dsp_ok && looper;
#endif
#if (AMB_CHAIN_LEVEL >= 2) && !defined(AMB_BUILTIN_REVERB)
        dsp_ok = dsp_ok && reverb;
#endif
#if AMB_CHAIN_LEVEL >= 3
        dsp_ok = dsp_ok && bloom && drift;
#endif
#if AMB_CHAIN_LEVEL >= 4
        dsp_ok = dsp_ok && harmony;
#endif
#if AMB_CHAIN_LEVEL >= 5
        dsp_ok = dsp_ok && microloop;
#endif
#if AMB_CHAIN_LEVEL >= 6
        dsp_ok = dsp_ok && granular;
#endif
        fc_init(&st, 0.7f);
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
    }

    static void note_cb(void* user, int voice, int note, unsigned char vel, finger_t f) {
        ambiotica_panel* self = (ambiotica_panel*)user;
        if (voice < 0 || voice >= 16) return;
        unsigned short bit = (unsigned short)(1u << voice);
        bool is_new = (self->voices_active & bit) == 0;
        play_synth(voice, self->synth_preset, (int)vel, note << 8, is_new);
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
        frame_ctr++;
        if ((frame_ctr % 60) == 0)   /* native-reverb wet level x1000 (calibrate kIn/kOut; want ~200-600, <1500) */
            printf("REVLVL in=%d wet=%d out=%d\n", (int)(viz_inpk * 1000.f), (int)(st.br_peak * 1000.f), (int)(viz_outpk * 1000.f));   /* in vs ~180 nominal; out>=1000 = clipping */
#ifdef AMB_DEBUG
        /* print per-stage metrics from core0 (~2x/sec). hf x1000: clean ~4-20;
         * the first stage that spikes is where the fizz is injected. */
        if ((frame_ctr % 120) == 0) {
            static const char* nm6[6] = {"src","loop","gran","mic","wet","out"};
            for (int s = 0; s < 6; s++)
                printf("%s pk=%d hf=%d  ", nm6[s], dbg_pk[s], dbg_hf[s]);
            printf("\n");
        }
#endif
        voices_seen = 0;
        /* play surface, now with an activity glow/sparkle via the brightness cb */
        /* 4-voice polyphony: the synth renders inside the same core1 2ms budget
         * as our FX; 8 voices' render time pushed us over. 4 leaves headroom for
         * the full chain and is plenty for an ambient wash. */
        play.do_play_surface(0, 0, 8, 16, 4, DIMMEST(TEAL), TEAL, 48, 3, note_cb, this,
                             VERTICAL | SHOW_BACKGROUND | STRINGOPHONIC_MONO, 0, -1,
                             viz_brightness, this);
        unsigned short released = (unsigned short)(voices_active & ~voices_seen);
        for (int v = 0; v < 16; v++) if (released & (1u << v)) synth_note_up(v);
        voices_active = voices_seen;
        static const char* nm[FX_N] = { "Orbit","Constellate","Satellite","Tail","Flux","Spectra","Mix" };
        /* Flux drives a shared shimmer LFO for the Tail+Flux sliders so they move
           together as one unit: rate follows Flux's mod rate, depth its amount. */
        float flux = fx_val[FX_FLUX] / 127.f;
        shimmer_phase += (float)dt_us * 1e-6f * (0.30f + fx.mod_rate * 2.0f);
        shimmer_phase -= (float)(int)shimmer_phase;                /* wrap to [0,1) */
        float tri = shimmer_phase < 0.5f ? shimmer_phase * 2.f : 2.f - shimmer_phase * 2.f;
        float shimmer = tri * tri * (3.f - 2.f * tri);             /* smoothstep, softer glow */
        for (int i = 0; i < FX_N; i++) {
            /* Reactive sliders pulse their colour; Spectra & Mix stay steady.
               (the *N.f gains below are the obvious brightness tuning knobs) */
            int bri = 256;
            switch (i) {
                case FX_ORBIT:       bri = meter_bri(viz_loop_env,  viz_loop_pk,  0.02f); break;  /* pulse: main loop emitting */
                case FX_SATELLITE:   bri = meter_bri(viz_micro_env, viz_micro_pk, 0.01f); break;  /* pulse: micro-loop emitting */
                case FX_CONSTELLATE: bri = meter_bri(viz_grain_env, viz_grain_pk, 0.01f); break;  /* pulse: grains firing */
                case FX_TAIL:
                case FX_FLUX:        bri = 60 + (int)(flux * shimmer * 196.f); break;  /* shimmer together as one unit */
            }
            if (bri < 0) bri = 0; if (bri > 256) bri = 256;
            fxslider[i].simple_slider(8 + i, 0, 16, VERTICAL | SHOW_STEM,
                                      fade_col(palette[8][i], bri), 0, 127, fx_val[i], nm[i]);
            fx_val[i] = (unsigned char)last_widget_new_value();

            /* Orbit & Satellite each carry a white "star" that falls down its own
               column once per loop cycle (main loop / micro-loop) — a per-column
               clock. Always visible; brighter as that loop emits. */
            if (i == FX_ORBIT || i == FX_SATELLITE) {
                unsigned int phase = (i == FX_ORBIT) ? viz_loop     : viz_micro;
                unsigned int len   = (i == FX_ORBIT) ? viz_loop_len : viz_micro_len;
                int row = len ? (int)(((float)phase / (float)len) * 15.f) : 0;
                if (row < 0) row = 0; if (row > 15) row = 15;
                int sb = 130 + (bri - 36) / 2;  if (sb > 256) sb = 256;
                set_led(8 + i, row, fade_col(WHITE, sb));
            }
        }
        push_fx_from_ui();
        /* col 15 is intentionally blank for now (to be repurposed later). */
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

#ifdef AMB_BUILTIN_REVERB
        /* Native-reverb warmup (no symbols needed): for the first blocks, return
         * false so the standard chain (do_fx) runs and RESOLVES our zeroed mix
         * preset (REVERB_SHIMMER=0) into the reverb's internal static config that
         * do_reverb() reads. Silence the buses so it's inaudible. Then own the
         * output and drive do_reverb ourselves — with (hopefully) shimmer off. */
        if (reverb_warmup < 96) {
            reverb_warmup++;
            memset(mix_buffers_out->dry,        0, sizeof(mix_buffers_out->dry));
            memset(mix_buffers_out->reverbsend, 0, sizeof(mix_buffers_out->reverbsend));
            memset(mix_buffers_out->delaysend,  0, sizeof(mix_buffers_out->delaysend));
            return false;   /* run do_fx -> resolves reverb config from the zeroed mix preset */
        }
#endif

#ifdef AMB_BYPASS
        /* DIAGNOSTIC: skip the entire Ambiotica chain — just pass the synth's dry
         * bus through my I/O path (in-gain + clip) and own the output. If this
         * fizzes, the artifact is the synth/I-O, NOT the FX chain. */
        const float kb = (1.0f / 32768.0f) * AMB_IN_GAIN;
        for (int i = 0; i < BLOCK_SIZE * 2; i++) {
            int v = (int)(mix_buffers_out->dry[i] * kb * 32767.0f);
            audiobuf_out[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
        return true;
#endif

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
        #undef AMB_SM
        fx_sm.loop_length_bars = fx.loop_length_bars; fx_sm.micro_bars = fx.micro_bars;
        fx_sm.bpm = fx.bpm; fx_sm.key = fx.key; fx_sm.chord = fx.chord;

        fc_render_block(&st, looper, granular, microloop, reverb, harmony, bloom, drift,
                        &fx_sm, AMB_SR, sL, sR, oL, oR, BLOCK_SIZE);

#ifdef AMB_DEBUG
        /* measure each stage on core1; on_ui prints them (core0) */
        dbg_measure(0, st.srcL);   /* input + drift regen */
        dbg_measure(1, st.loopL);  /* looper              */
        dbg_measure(2, st.granL);  /* granular            */
        dbg_measure(3, st.micL);   /* micro-loop          */
        dbg_measure(4, st.wetL);   /* reverb + Spectra     */
        dbg_measure(5, oL);        /* final               */
#endif
        for (int i = 0; i < BLOCK_SIZE; i++) {
            int l = (int)(oL[i] * 32767.0f), r = (int)(oR[i] * 32767.0f);
            audiobuf_out[2*i]   = (int16_t)(l < -32768 ? -32768 : l > 32767 ? 32767 : l);
            audiobuf_out[2*i+1] = (int16_t)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
        }

        /* cheap visualization taps: output level (breathing) + per-channel block
           peaks for the main loop, micro-loop and granular. */
        float op = 0.f, gp = 0.f, lp = 0.f, mp = 0.f, ip = 0.f;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            float a = oL[i]       < 0.f ? -oL[i]       : oL[i];       if (a > op) op = a;
            float g = st.granL[i] < 0.f ? -st.granL[i] : st.granL[i]; if (g > gp) gp = g;
            float l = st.loopL[i] < 0.f ? -st.loopL[i] : st.loopL[i]; if (l > lp) lp = l;
            float m = st.micL[i]  < 0.f ? -st.micL[i]  : st.micL[i];  if (m > mp) mp = m;
            float s = sL[i]       < 0.f ? -sL[i]       : sL[i];       if (s > ip) ip = s;   /* chain input, post AMB_IN_GAIN */
        }
        viz_out       = viz_out * 0.9f + op * 0.1f;
        viz_outpk     = viz_outpk > op ? viz_outpk * 0.97f : op;    /* DEBUG: final output peak-hold */
        viz_inpk      = viz_inpk > ip ? viz_inpk * 0.97f : ip;      /* DEBUG: chain input peak-hold */
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

    bool on_serialise(serialiser_t& s, int version) override {
        /* TODO: persist fx_val[], synth_preset, fx.key/chord via save_and_load.h field macros */
        return panel_t::on_serialise(s, version);
    }
};
