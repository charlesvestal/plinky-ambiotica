/* Ambiotica-on-Plinky — panel scaffold.
 *
 * Built-in synth (play surface, left half) -> Ambiotica chain -> output.
 * Ambiotica is inserted on the synth's dry stereo in on_dsp_final_mix; Plinky's
 * own do_fx (delay/reverb) is left unused because Ambiotica IS the FX.
 *
 * Memory: looper/granular/microloop/reverb -> PSRAM (get_psram_ptr); harmony/
 * drift/bloom + all small state -> the panel's SRAM pool (128 KB arena). Chain
 * scratch + persistent state live in fc_state (a member), never the stack.
 *
 * ================= VERIFY IN EMULATOR (blind-coded vs llm.txt) =================
 *  1. Sample rate constant (AMB_SR) — assumed 32000. Confirm Plinky's DSP rate.
 *  2. on_dsp_final_mix(audiobuf_in, dry_stereo): dry_stereo is int stereo
 *     interleaved, BLOCK_SIZE frames, ~int16 range. Confirm scale (±32767) and
 *     that this is the right hook to post-process the synth (and that we may
 *     leave delaysend/reverbsend untouched to bypass do_fx).
 *  3. play_surface_t member + do_play_surface(...) signature/flags; note_cb type
 *     (play_surface_note_fn) and finger_t. Confirm play_synth() usable here.
 *  4. Built-in synth preset: which preset_idx makes sound; may need to load one.
 *  5. slider_t::simple_slider(...) args + last_widget_new_value() read-back.
 *  6. palette[8][i], DIMMEST(), leds_clear(), BLOCK_SIZE, get_psram_ptr/size.
 *  7. on_serialise field-macro API (save_and_load.h) — persistence stubbed.
 *  8. libm availability (expf/cosf/tanhf/powf/lrintf) in the panel toolchain.
 *  9. reverb (150 KB, scattered) lives in PSRAM -> measure CPU; the #1 perf risk.
 * =============================================================================
 */
#define PANEL_PAD_COLOR TEAL
#define AMB_SR 32000.0                 /* VERIFY (1) */

enum { FX_ORBIT, FX_CONSTELLATE, FX_SATELLITE, FX_TAIL, FX_FLUX, FX_SPECTRA, FX_MIX, FX_N };

struct ambiotica_panel : panel_t {
    /* DSP modules (allocated from the arenas in setup_default_panel_state) */
    looper_t*    looper    = 0;
    granular_t*  granular  = 0;
    microloop_t* microloop = 0;
    reverb_t*    reverb    = 0;
    harmony_t*   harmony   = 0;
    bloom_t*     bloom     = 0;
    drift_t*     drift     = 0;

    full_params fx;                    /* written by on_ui (core0), read by DSP (core1) */
    fc_state    st;                    /* chain persistent + scratch state (~22 KB) */

    /* SRAM pool for small module state (harmony ~27 KB + drift + bloom + structs).
     * Kept comfortably under the 128 KB panel-object budget alongside fc_state. */
    unsigned char sram_pool[64 * 1024];

    /* per-block float scratch for the int<->float conversion (off the stack) */
    float sL[BLOCK_SIZE], sR[BLOCK_SIZE], oL[BLOCK_SIZE], oR[BLOCK_SIZE];

    play_surface_t play;               /* VERIFY (3) */
    slider_t       fxslider[FX_N];
    unsigned char  fx_val[FX_N];       /* 0..127 UI values (serialised) */
    int            synth_preset = 0;    /* VERIFY (4) */

    /* ---------------- lifecycle ---------------- */
    void setup_default_panel_state() override {
        /* wire the arenas */
        g_amb_ps_base = get_psram_ptr(); g_amb_ps_cap = get_psram_size(); g_amb_ps_used = 0;
        g_amb_sr_base = sram_pool;       g_amb_sr_cap = sizeof(sram_pool);  g_amb_sr_used = 0;

        const int sr = (int)AMB_SR;
        const int loopcap = 32 * sr;   /* 32 s max loop (int16 bed -> 4 MB) */

        g_amb_region = 1;              /* -> PSRAM */
        looper    = looper_create(loopcap, sr);
        granular  = granular_create(sr);
        microloop = microloop_create(sr);
        reverb    = reverb_create(sr);          /* VERIFY (9): scattered in PSRAM */
        g_amb_region = 0;             /* -> SRAM pool */
        harmony   = harmony_create(sr);
        bloom     = bloom_create(sr);
        drift     = drift_create(sr);

        fc_init(&st, 0.7f);

        /* default macro positions (0..127) */
        fx_val[FX_ORBIT] = 48; fx_val[FX_CONSTELLATE] = 48; fx_val[FX_SATELLITE] = 32;
        fx_val[FX_TAIL] = 76; fx_val[FX_FLUX] = 40; fx_val[FX_SPECTRA] = 64; fx_val[FX_MIX] = 90;
        memset(&fx, 0, sizeof fx);
        fx.bpm = 120.f; fx.loop_length_bars = 2.f; fx.key = 0; fx.chord = 0; fx.bloom = 0.4f;
        push_fx_from_ui();
    }

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

    /* play surface -> built-in synth voice */
    static void note_cb(void* user, int voice, int note, unsigned char vel, finger_t f) {  /* VERIFY (3) */
        ambiotica_panel* self = (ambiotica_panel*)user;
        play_synth(voice, self->synth_preset, (int)vel, note << 8, /*retrigger*/ true);
        (void)f;
    }

    void on_ui(int dt_us) override {
        (void)dt_us;
        leds_clear();

        /* LEFT 8 cols: play surface driving the built-in synth (VERIFY 3/4) */
        play.do_play_surface(0, 0, 8, 16, /*max_voices*/ 8, DIMMEST(TEAL), TEAL,
                             /*starting_pitch*/ 48, /*interval_degrees*/ 3,
                             note_cb, this);

        /* RIGHT 8 cols: one vertical FX macro slider per column (VERIFY 5/6) */
        static const char* nm[FX_N] = { "Orbit","Constellate","Satellite","Tail","Flux","Spectra","Mix" };
        for (int i = 0; i < FX_N; i++) {
            fxslider[i].simple_slider(8 + i, 0, 16, VERTICAL | SHOW_STEM,
                                      palette[8][i], 0, 127, fx_val[i], nm[i]);
            fx_val[i] = (unsigned char)last_widget_new_value();
        }
        push_fx_from_ui();
    }

    /* ---------------- audio: Ambiotica insert on the synth dry (core1) ---------------- */
    void on_dsp_final_mix(const int16_t* audiobuf_in, int* dry_stereo) override {   /* VERIFY (2) */
        (void)audiobuf_in;
        const float k = 1.0f / 32768.0f;
        for (int i = 0; i < BLOCK_SIZE; i++) { sL[i] = dry_stereo[2*i] * k; sR[i] = dry_stereo[2*i+1] * k; }

        fc_render_block(&st, looper, granular, microloop, reverb, harmony, bloom, drift,
                        &fx, AMB_SR, sL, sR, oL, oR, BLOCK_SIZE);

        for (int i = 0; i < BLOCK_SIZE; i++) {
            dry_stereo[2*i]   = (int)(oL[i] * 32767.0f);
            dry_stereo[2*i+1] = (int)(oR[i] * 32767.0f);
        }
    }

    /* ---------------- persistence (song slots) — VERIFY (7) ---------------- */
    bool on_serialise(serialiser_t& s, int version) override {
        /* TODO: use the field macros from save_and_load.h to persist:
         *   fx_val[FX_N], synth_preset, fx.bpm, fx.key, fx.chord
         * then rebuild derived state with push_fx_from_ui(). */
        return panel_t::on_serialise(s, version);
    }
};
