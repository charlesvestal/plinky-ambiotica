/* Ambiotica drums — see drums.h for why these sit outside the wash.
 *
 * ORIGINAL code (not vendored from ambiotica-plugin).
 *
 * The kit is synthesised once at create() into 8-bit mono PSRAM buffers, then played back by
 * plain pointer walks. That split is deliberate: all the transcendental maths happens once at
 * boot where nothing is realtime, and the per-block cost collapses to a read, a multiply and
 * an accumulate per voice. Eight voices over a 64-frame block is ~512 of those, which is what
 * lets a full kit fit in the ~250us core1 leaves us.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "drums.h"

typedef struct {
    signed char* data;      /* 8-bit mono, PSRAM */
    int          len;
    float        gain;      /* per-track trim, baked at synthesis time */
    float        pan;       /* -1 = hard L, +1 = hard R; kept narrow for a centred kit */
} drum_sample_t;

typedef struct {
    float pos;              /* fractional read head; <0 = idle */
    float amp;              /* velocity, 0..1 */
} drum_voice_t;

/* A track either plays its generated 8-bit buffer or a slice of preset-owned sample memory.
 * va_end > va_start selects the latter. Sample memory is 16-bit and paged behind
 * chunk_location_ptrs, so it is read through READ_SAMPLE rather than a raw pointer — the
 * chunk a slice lives in can move. */
typedef struct { unsigned int va_start, va_end; } drum_source_t;

struct drums_s {
    drum_sample_t s[DRUM_TRACKS];
    drum_voice_t  v[DRUM_TRACKS];   /* one voice per track — a retrigger cuts its predecessor */
    drum_source_t src[DRUM_TRACKS]; /* zeroed = use the generated buffer */
    int           choke;            /* closed hat cuts open hat — a kit thing, not a break thing */
    float         rate;             /* read speed; 1 = unity, 2 = an octave up */
};

/* ---- synthesis helpers (boot-time only, never realtime) ---------------------------- */

/* xorshift32 — the noise source for hats, snare and clap. Seeded per-sample so a given kit
   is bit-identical on every boot; a kit that changes each power cycle would be a bug. */
static unsigned int drum_rng(unsigned int* s) {
    unsigned int x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (*s = x);
}
static float drum_noise(unsigned int* s) {
    return (float)((int)(drum_rng(s) >> 8) - 8388608) * (1.0f / 8388608.0f);
}

/* Write one float sample, clipped, as 8-bit. Peak-normalisation happens afterwards in
   drum_finish so each voice uses the full 8-bit range rather than wasting it on headroom. */
static void drum_store(float* tmp, int n, signed char* dst, int len) {
    float peak = 0.f;
    for (int i = 0; i < n; i++) { float a = tmp[i] < 0 ? -tmp[i] : tmp[i]; if (a > peak) peak = a; }
    float k = peak > 1e-6f ? 127.0f / peak : 0.f;
    for (int i = 0; i < n; i++) {
        int q = (int)(tmp[i] * k);
        dst[i] = (signed char)(q < -127 ? -127 : q > 127 ? 127 : q);
    }
    for (int i = n; i < len; i++) dst[i] = 0;
}

drums_t* drums_create(double sample_rate) {
    drums_t* d = (drums_t*)calloc(1, sizeof(drums_t));
    if (!d) return 0;
    const float sr = (float)sample_rate;
    const float dt = 1.0f / sr;

    /* Lengths chosen so the whole kit is ~57 KB at 8-bit: long enough for the tails that
       matter (kick, open hat) and brutally short where they do not (rim, closed hat). */
    const float secs[DRUM_TRACKS] = { 0.35f, 0.22f, 0.05f, 0.30f, 0.25f, 0.06f, 0.30f, 0.25f };
    int maxlen = 0;
    for (int t = 0; t < DRUM_TRACKS; t++) {
        d->s[t].len = (int)(secs[t] * sr);
        if (d->s[t].len > maxlen) maxlen = d->s[t].len;
        d->s[t].data = (signed char*)calloc(1, (size_t)d->s[t].len);
        if (!d->s[t].data) return 0;
    }
    /* One scratch buffer, reused per track and released implicitly (arena, no frees). */
    float* tmp = (float*)calloc((size_t)maxlen, sizeof(float));
    if (!tmp) return 0;

    unsigned int rng = 0x1234567u;

    /* KICK — pitch sweep 120 -> 45 Hz over ~40 ms with a fast body decay, plus a click
       transient so it reads on small speakers where the fundamental does not survive. */
    {
        int n = d->s[DRUM_KICK].len; float ph = 0.f;
        for (int i = 0; i < n; i++) {
            float t = i * dt;
            float f = 45.0f + 75.0f * expf(-t * 28.0f);
            ph += f * dt; if (ph > 1.f) ph -= 1.f;
            float body  = sinf(ph * 6.2831853f) * expf(-t * 7.0f);
            float click = expf(-t * 400.0f) * 0.5f;
            tmp[i] = body + click;
        }
        drum_store(tmp, n, d->s[DRUM_KICK].data, n);
        d->s[DRUM_KICK].gain = 1.00f; d->s[DRUM_KICK].pan = 0.00f;
    }
    /* SNARE — 190 Hz body under a noise crack, the noise decaying slower than the tone so
       the tail is mostly rattle rather than pitch. */
    {
        int n = d->s[DRUM_SNARE].len; float ph = 0.f;
        for (int i = 0; i < n; i++) {
            float t = i * dt;
            ph += 190.0f * dt; if (ph > 1.f) ph -= 1.f;
            float body  = sinf(ph * 6.2831853f) * expf(-t * 28.0f) * 0.6f;
            float crack = drum_noise(&rng) * expf(-t * 18.0f);
            tmp[i] = body + crack;
        }
        drum_store(tmp, n, d->s[DRUM_SNARE].data, n);
        d->s[DRUM_SNARE].gain = 0.85f; d->s[DRUM_SNARE].pan = -0.08f;
    }
    /* HATS — noise through a crude one-pole high-pass (sample minus its running average).
       Closed and open differ only in decay, as on the machines this imitates. */
    for (int pass = 0; pass < 2; pass++) {
        int trk = pass ? DRUM_OHAT : DRUM_CHAT;
        int n = d->s[trk].len;
        float decay = pass ? 12.0f : 70.0f, lp = 0.f;
        for (int i = 0; i < n; i++) {
            float t = i * dt;
            float w = drum_noise(&rng);
            lp += (w - lp) * 0.55f;              /* running average ... */
            tmp[i] = (w - lp) * expf(-t * decay); /* ... subtracted = high-pass */
        }
        drum_store(tmp, n, d->s[trk].data, n);
        d->s[trk].gain = pass ? 0.42f : 0.38f;
        d->s[trk].pan  = pass ? 0.18f : 0.14f;
    }
    /* CLAP — four noise bursts ~9 ms apart, then a longer diffuse tail. The bursts are what
       make it read as a clap rather than a short snare. */
    {
        int n = d->s[DRUM_CLAP].len; float lp = 0.f;
        int burst = (int)(0.009f * sr);
        for (int i = 0; i < n; i++) {
            float t = i * dt;
            float w = drum_noise(&rng);
            lp += (w - lp) * 0.5f;
            float hp = w - lp;
            float env;
            if (i < burst * 4) {
                int within = i % burst;
                env = expf(-(within * dt) * 220.0f);       /* sharp per-burst */
            } else {
                env = expf(-(t - burst * 4 * dt) * 14.0f) * 0.7f;   /* tail */
            }
            tmp[i] = hp * env;
        }
        drum_store(tmp, n, d->s[DRUM_CLAP].data, n);
        d->s[DRUM_CLAP].gain = 0.62f; d->s[DRUM_CLAP].pan = 0.22f;
    }
    /* RIM — a very short, very fast-decaying high tone. Almost a click with a pitch. */
    {
        int n = d->s[DRUM_RIM].len; float ph = 0.f;
        for (int i = 0; i < n; i++) {
            float t = i * dt;
            ph += 1700.0f * dt; if (ph > 1.f) ph -= 1.f;
            tmp[i] = sinf(ph * 6.2831853f) * expf(-t * 150.0f);
        }
        drum_store(tmp, n, d->s[DRUM_RIM].data, n);
        d->s[DRUM_RIM].gain = 0.50f; d->s[DRUM_RIM].pan = -0.22f;
    }
    /* TOMS — the kick's shape with a gentler sweep and a slower decay, at two pitches. */
    for (int pass = 0; pass < 2; pass++) {
        int trk = pass ? DRUM_TOMHI : DRUM_TOMLO;
        int n = d->s[trk].len; float ph = 0.f;
        float base = pass ? 180.0f : 110.0f;
        for (int i = 0; i < n; i++) {
            float t = i * dt;
            float f = base * (1.0f + 0.35f * expf(-t * 22.0f));
            ph += f * dt; if (ph > 1.f) ph -= 1.f;
            tmp[i] = sinf(ph * 6.2831853f) * expf(-t * 9.0f);
        }
        drum_store(tmp, n, d->s[trk].data, n);
        d->s[trk].gain = 0.70f;
        d->s[trk].pan  = pass ? 0.30f : -0.30f;
    }

    for (int t = 0; t < DRUM_TRACKS; t++) d->v[t].pos = -1.0f;
    d->choke = 1;
    d->rate  = 1.0f;
    return d;
}

void drums_set_choke(drums_t* d, int enabled) { if (d) d->choke = enabled ? 1 : 0; }

void drums_trigger(drums_t* d, int track, int velocity) {
    if (!d || track < 0 || track >= DRUM_TRACKS || velocity <= 0) return;
    /* A closed hat chokes the open hat — the one piece of cross-track behaviour worth
       having, and the thing that makes a hat pattern sound like a hat pattern. */
    if (d->choke && track == DRUM_CHAT) d->v[DRUM_OHAT].pos = -1.0f;
    d->v[track].pos = 0.0f;
    d->v[track].amp = (float)velocity * (1.0f / 127.0f);
}

void drums_set_pitch(drums_t* d, float semitones) {
    if (!d) return;
    /* 2^(semi/12). Clamped so a stray value cannot walk the read head backwards or so fast
       that a slice is skipped entirely between blocks. */
    if (semitones < -24.f) semitones = -24.f;
    if (semitones >  24.f) semitones =  24.f;
    d->rate = powf(2.0f, semitones / 12.0f);
}

void drums_all_off(drums_t* d) {
    if (!d) return;
    for (int t = 0; t < DRUM_TRACKS; t++) d->v[t].pos = -1.0f;
}

void drums_set_sample(drums_t* d, int track, unsigned int va_start, unsigned int va_end) {
    if (!d || track < 0 || track >= DRUM_TRACKS) return;
    if (va_end <= va_start) { d->src[track].va_start = d->src[track].va_end = 0; }
    else                    { d->src[track].va_start = va_start; d->src[track].va_end = va_end; }
    d->v[track].pos = -1.0f;   /* a voice mid-flight would keep indexing the old source */
}

void PLINKY_DSP_RAM_FUNC(drums_render)(drums_t* d, float* out_l, float* out_r, int frames) {
    if (!d) return;
    for (int t = 0; t < DRUM_TRACKS; t++) {
        drum_voice_t* v = &d->v[t];
        if (v->pos < 0) continue;
        const drum_sample_t* s = &d->s[t];
        const drum_source_t* src = &d->src[t];
        const int sampled = (src->va_end > src->va_start);
        const int len = sampled ? (int)(src->va_end - src->va_start) : s->len;
        /* 8-bit generated buffers run +/-127, preset sample memory is 16-bit; normalise here
           so per-track gain and velocity mean the same thing either way. */
        /* Per-track gain/pan are trims baked for the GENERATED kit; applying the synthesised
           hat's 0.38 to a sampled hat (or to an eighth of a break) would be arbitrary, so
           sampled sources play flat and centred. */
        float g  = v->amp * (sampled ? (1.0f / 32768.0f) : s->gain * (1.0f / 127.0f));
        float pan = sampled ? 0.0f : s->pan;
        /* Equal-ish power pan, cheap: pan is small so a linear split is inaudible here. */
        float gl = g * (1.0f - pan) * 0.5f;
        float gr = g * (1.0f + pan) * 0.5f;
        /* Resampled read head. Linear interpolation between neighbouring samples: without it
           a transposed drum picks up hard quantisation noise on top of the aliasing. The
           SDK does precompute prefiltered mipmaps for exactly this (get_mip_va), which would
           beat interpolation when pitching well above unity — but how a slice's offsets map
           into mip space is undocumented, so mip 0 it is until that is worth confirming. */
        const float rate = d->rate;
        float pos = v->pos;
        const float last = (float)(len - 1);
        /* Unity pitch is the common case — the kit is untransposed unless you ask — and
           there interpolation is pure waste: the read head lands exactly on samples. That
           matters more than it looks, because the expensive part is not the arithmetic but
           the READ_SAMPLE lookups: each one indexes the chunk table and reaches into paged
           memory, and interpolating does TWO per output sample. Profiling put this stage at
           ~180us for eight voices, roughly 50 cycles a sample, which is the double read
           rather than the maths. So walk integers at unity and only interpolate when
           actually resampling. */
        if (rate == 1.0f) {
            int i0 = (int)pos;
            for (int i = 0; i < frames; i++, i0++) {
                if ((float)i0 >= last) { i0 = -1; break; }
                float x;
                if (sampled) {
#if defined(READ_SAMPLE)
                    x = (float)(int16_t)READ_SAMPLE(src->va_start + (unsigned int)i0);
#else
                    x = 0.0f;
#endif
                } else x = (float)s->data[i0];
                out_l[i] += x * gl;
                out_r[i] += x * gr;
            }
            pos = (i0 < 0) ? -1.0f : (float)i0;
        } else {
            for (int i = 0; i < frames; i++) {
                if (pos >= last) { pos = -1.0f; break; }
                int   i0 = (int)pos;
                float fr = pos - (float)i0;
                float a, b;
                if (sampled) {
#if defined(READ_SAMPLE)
                    unsigned int va = src->va_start + (unsigned int)i0;
                    a = (float)(int16_t)READ_SAMPLE(va);
                    b = (float)(int16_t)READ_SAMPLE(va + 1u);
#else
                    a = b = 0.0f;
#endif
                } else {
                    a = (float)s->data[i0];
                    b = (float)s->data[i0 + 1];
                }
                float x = a + (b - a) * fr;
                out_l[i] += x * gl;
                out_r[i] += x * gr;
                pos += rate;
            }
        }
        v->pos = pos;
    }
}
