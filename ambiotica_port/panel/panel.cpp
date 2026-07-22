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
 *   5 dsp     — + Ambiotica inserted in on_dsp_final_mix (full panel).
 *
 * Flash 1..5 in order; the first that hangs on the loading screen pinpoints it.
 *
 * ============== VERIFY IN EMULATOR (blind-coded vs llm.txt) ==============
 *  sample rate (AMB_SR=32000); on_dsp_final_mix scale/hook; play_surface_t /
 *  do_play_surface / play_synth / synth preset; slider_t / last_widget_new_value;
 *  palette / DIMMEST / BLOCK_SIZE; on_serialise field macros; reverb-in-PSRAM CPU.
 * ========================================================================
 */
#ifndef AMB_STAGE
#define AMB_STAGE 5
#endif

#define PANEL_PAD_COLOR TEAL
#define AMB_SR 32000.0

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
#if AMB_CHAIN_LEVEL >= 4
    unsigned char sram_pool[106 * 1024];   /* reverb(79) + harmony 4-voice(18) + drift(6) */
#elif AMB_CHAIN_LEVEL >= 2
    unsigned char sram_pool[88 * 1024];    /* reverb (+ drift/bloom at L3) */
#else
    unsigned char sram_pool[1 * 1024];     /* L0/L1: no SRAM DSP */
#endif
    float sL[BLOCK_SIZE], sR[BLOCK_SIZE], oL[BLOCK_SIZE], oR[BLOCK_SIZE];
    play_surface_t play;
    slider_t       fxslider[FX_N];
    unsigned char  fx_val[FX_N];
    int            synth_preset = 0;
    unsigned short voices_active = 0, voices_seen = 0;
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
        fx.loop_layer       = fx_val[FX_ORBIT] / 127.f;
        fx.loop_length_bars = 0.5f + 7.5f * (fx_val[FX_ORBIT] / 127.f);
        fx.grain_size = fx.scatter = fx_val[FX_CONSTELLATE] / 127.f;
        fx.micro_hold       = fx_val[FX_SATELLITE] / 127.f;
        fx.decay = fx.ring   = fx_val[FX_TAIL] / 127.f;
        fx.mod_depth = fx.mod_rate = fx.drift_amt = fx_val[FX_FLUX] / 127.f;
        fx.spectra          = fx_val[FX_SPECTRA] / 127.f;
        fx.mix              = fx_val[FX_MIX] / 127.f;
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
        voices_seen = 0;
        play.do_play_surface(0, 0, 8, 16, 8, DIMMEST(TEAL), TEAL, 48, 3, note_cb, this);
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
#endif
    }

#if AMB_STAGE >= 5
    void on_dsp_final_mix(const int16_t* audiobuf_in, int* dry_stereo) override {
        (void)audiobuf_in;
        if (!dsp_ok) return;
        const float k = 1.0f / 32768.0f;
        for (int i = 0; i < BLOCK_SIZE; i++) { sL[i] = dry_stereo[2*i] * k; sR[i] = dry_stereo[2*i+1] * k; }
        fc_render_block(&st, looper, granular, microloop, reverb, harmony, bloom, drift,
                        &fx, AMB_SR, sL, sR, oL, oR, BLOCK_SIZE);
        for (int i = 0; i < BLOCK_SIZE; i++) {
            dry_stereo[2*i]   = (int)(oL[i] * 32767.0f);
            dry_stereo[2*i+1] = (int)(oR[i] * 32767.0f);
        }
    }
#endif

#if AMB_STAGE >= 2
    bool on_serialise(serialiser_t& s, int version) override {
        /* TODO (VERIFY): persist fx_val[], synth_preset, fx.key/chord via save_and_load.h field macros */
        return panel_t::on_serialise(s, version);
    }
#endif
};
