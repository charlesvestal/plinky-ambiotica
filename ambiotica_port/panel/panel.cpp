/* Ambiotica-on-Plinky — panel (with AMB_STAGE bisection switch).
 *
 * AMB_STAGE gates how much the panel does, to localize an on-device load crash
 * WITHOUT a serial console. Each stage paints the grid so you can see it loaded:
 *   1 bare    — just lights the grid TEAL. Proves the panel shell loads.
 *   2 members — + the big member arrays (unused). Grid BLUE. Tests object size
 *               vs the 128 KB arena.
 *   3 modules — + create all 7 DSP modules (6.9 MB PSRAM alloc + zeroing). Grid
 *               GREEN if all allocated / RED if not; bottom row = a bar of the
 *               reported PSRAM size in MB. Tests PSRAM + the big memset-at-load.
 *   4 play    — + play surface driving the built-in synth. Normal left/right UI.
 *   5 dsp     — + Ambiotica in on_dsp (core1), owns audio_out (full panel).
 *
 * Flash 1..5 in order; the first that hangs on the loading screen pinpoints it.
 *
 * ============== VERIFY IN EMULATOR (blind-coded vs llm.txt) ==============
 *  sample rate (AMB_SR=32000); on_dsp core1 hook + mix_buffers_out->dry scale; play_surface_t /
 *  do_play_surface / play_synth / synth preset; slider_t / last_widget_new_value;
 *  palette / DIMMEST / BLOCK_SIZE; on_serialise field macros; reverb-in-PSRAM CPU.
 * ========================================================================
 */
#ifndef AMB_STAGE
#define AMB_STAGE 5
#endif

#define PANEL_PAD_COLOR TEAL
#define AMB_SR 32000.0
#define FC_SOFT_CLIP            /* soft-limit the chain output (see full_chain.h) */
/* Attenuate the Plinky synth before the chain: up to 8 polyphonic voices sum far
 * hotter than the plugin's single input, so without headroom multiple notes slam
 * the ceiling and alias. Tune if too quiet/hot. */
#define AMB_IN_GAIN 0.35f

enum { FX_ORBIT, FX_CONSTELLATE, FX_SATELLITE, FX_TAIL, FX_FLUX, FX_SPECTRA, FX_MIX, FX_N };

struct ambiotica_panel : panel_t {
#if AMB_STAGE >= 3
    looper_t* looper = 0; granular_t* granular = 0; microloop_t* microloop = 0; reverb_t* reverb = 0;
    harmony_t* harmony = 0; bloom_t* bloom = 0; drift_t* drift = 0;
    bool dsp_ok = false;
    unsigned long psram_bytes = 0;
#endif
#if AMB_STAGE >= 2
    full_params fx;
    fc_state    st;
    /* Reverb (~79 KB, 4-comb) lives here now, not PSRAM — its scattered
     * delay-line access was ~10x slower in PSRAM and blew the core0 budget.
     * Sized per chain level so low levels keep the panel object small. */
#if AMB_CHAIN_LEVEL >= 2
    /* One size for all L>=2 (L3 proved 88 KB fits the arena). L2/L3 = 4-comb
     * reverb(79)+drift(6)=85 KB; L4+ = 3-comb reverb(62)+harmony 3v(14)+drift(6)
     * =82 KB. Micro-loop/granular (L5/L6) stay in PSRAM, not this pool. */
    unsigned char sram_pool[88 * 1024];
#else
    unsigned char sram_pool[1 * 1024];     /* L0/L1: no SRAM DSP */
#endif
    float sL[BLOCK_SIZE], sR[BLOCK_SIZE], oL[BLOCK_SIZE], oR[BLOCK_SIZE];
    play_surface_t play;
    slider_t       fxslider[FX_N];
    unsigned char  fx_val[FX_N];
    int            synth_preset = 0;
    unsigned short voices_active = 0, voices_seen = 0;

    /* visualization taps — written in on_dsp (core1 audio), read in on_ui */
    float        viz_out = 0.f, viz_grain = 0.f;   /* output level, grain-firing activity */
    unsigned int viz_loop = 0, frame_ctr = 0;      /* loop-cycle sample counter, UI frame */

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
#endif

    void setup_default_panel_state() override {
#if AMB_STAGE >= 2
        fx_val[FX_ORBIT] = 48; fx_val[FX_CONSTELLATE] = 48; fx_val[FX_SATELLITE] = 32;
        fx_val[FX_TAIL] = 76; fx_val[FX_FLUX] = 40; fx_val[FX_SPECTRA] = 64; fx_val[FX_MIX] = 90;
        memset(&fx, 0, sizeof fx);
        fx.bpm = 120.f; fx.loop_length_bars = 2.f; fx.key = 0; fx.chord = 0; fx.bloom = 0.4f;
        push_fx_from_ui();
#endif
#if AMB_STAGE >= 3
        g_amb_ps_base = get_psram_ptr(); g_amb_ps_cap = get_psram_size(); g_amb_ps_used = 0;
        g_amb_sr_base = sram_pool;       g_amb_sr_cap = sizeof(sram_pool);  g_amb_sr_used = 0;
        psram_bytes = (unsigned long) g_amb_ps_cap;
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
#if AMB_CHAIN_LEVEL >= 2
        reverb  = reverb_create(sr);        /* moved here (was PSRAM -> too slow) */
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
#if AMB_CHAIN_LEVEL >= 2
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
#endif
    }

#if AMB_STAGE >= 2
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
#endif

#if AMB_STAGE >= 4
    static void note_cb(void* user, int voice, int note, unsigned char vel, finger_t f) {
        ambiotica_panel* self = (ambiotica_panel*)user;
        if (voice < 0 || voice >= 16) return;
        unsigned short bit = (unsigned short)(1u << voice);
        bool is_new = (self->voices_active & bit) == 0;
        play_synth(voice, self->synth_preset, (int)vel, note << 8, is_new);
        self->voices_seen |= bit;
        (void)f;
    }

    /* play-surface glow: breathes with the wash, sparkles as grains fire.
       Signature matches do_play_surface's brightness callback (VERIFY). */
    static unsigned char viz_brightness(void* user, int si, int sp, int x, int y, int note) {
        (void)si; (void)sp; (void)note;
        ambiotica_panel* self = (ambiotica_panel*)user;
        int b = (int)(self->viz_out * 28.f);                       /* breathing base glow */
        if (self->viz_grain > 0.03f) {                             /* grain sparkles */
            unsigned h = pcg_hash((unsigned)(x * 131 + y * 977 + (self->frame_ctr >> 2)));
            if ((h & 31u) < (unsigned)(self->viz_grain * 44.f))
                b += (int)(self->viz_grain * 150.f);
        }
        if (b < 0) b = 0; if (b > 255) b = 255;
        return (unsigned char)b;
    }
#endif

    void on_ui(int dt_us) override {
        (void)dt_us;
        leds_clear();

#if AMB_STAGE == 1
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
            set_led(x, y, ((x + y) & 1) ? TEAL : DIMMEST(TEAL));
#elif AMB_STAGE == 2
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
            set_led(x, y, ((x + y) & 1) ? BLUE : DIMMEST(BLUE));
#elif AMB_STAGE == 3
        uint32_t c = dsp_ok ? GREEN : RED;
        for (int y = 0; y < 15; y++) for (int x = 0; x < 16; x++)
            set_led(x, y, ((x + y) & 1) ? c : DIMMEST(c));
        int mb = (int)(psram_bytes / (1024 * 1024));   /* PSRAM size as a bottom-row bar */
        for (int x = 0; x < 16; x++) set_led(x, 15, x < mb ? WHITE : DIMMEST(WHITE));
#else   /* stages 4 and 5: real UI */
        frame_ctr++;
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
        for (int i = 0; i < FX_N; i++) {
            fxslider[i].simple_slider(8 + i, 0, 16, VERTICAL | SHOW_STEM,
                                      palette[8][i], 0, 127, fx_val[i], nm[i]);
            fx_val[i] = (unsigned char)last_widget_new_value();
        }
        push_fx_from_ui();
        /* col 15 = loop clock: a dot that rises once per loop cycle, pulsing with level */
        {
            int llen = (int)(fx.loop_length_bars * 4.f * 60.f / (fx.bpm > 0.f ? fx.bpm : 120.f) * (float)AMB_SR);
            if (llen < 1) llen = 1;
            int row = (int)(((float)viz_loop / (float)llen) * 16.f); if (row > 15) row = 15;
            for (int y = 0; y < 16; y++)
                set_led(15, y, (y == row) ? fade_col(WHITE, 120 + (int)(viz_out * 130.f))
                                          : (y == 0 ? DIMMEST(TEAL) : 0));
        }
#endif
    }

#if AMB_STAGE >= 5
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
        fc_render_block(&st, looper, granular, microloop, reverb, harmony, bloom, drift,
                        &fx, AMB_SR, sL, sR, oL, oR, BLOCK_SIZE);

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

        /* cheap visualization taps: output level, grain activity, loop phase */
        float op = 0.f, gp = 0.f;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            float a = oL[i] < 0.f ? -oL[i] : oL[i];                   if (a > op) op = a;
            float g = st.granL[i] < 0.f ? -st.granL[i] : st.granL[i]; if (g > gp) gp = g;
        }
        viz_out   = viz_out * 0.9f + op * 0.1f;
        viz_grain = viz_grain > gp ? viz_grain * 0.85f : gp;
        int llen = (int)(fx.loop_length_bars * 4.f * 60.f / (fx.bpm > 0.f ? fx.bpm : 120.f) * (float)AMB_SR);
        if (llen < 1) llen = 1;
        viz_loop += BLOCK_SIZE; if (viz_loop >= (unsigned)llen) viz_loop -= (unsigned)llen;
        return true;
    }
#endif

#if AMB_STAGE >= 2
    bool on_serialise(serialiser_t& s, int version) override {
        /* TODO (VERIFY): persist fx_val[], synth_preset, fx.key/chord via save_and_load.h field macros */
        return panel_t::on_serialise(s, version);
    }
#endif
};
