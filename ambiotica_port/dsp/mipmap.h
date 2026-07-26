/* Where a sample slice lives in mip space.
 *
 * The SDK precomputes prefiltered octave versions of every sample ("mipmaps") to reduce
 * aliasing when playing above unity pitch, and exposes get_mip_va to find each octave's base
 * address. What it does NOT document is how a SLICE's offsets map into those octaves. This
 * header is that mapping, derived from get_mip_va and then confirmed against hardware.
 *
 * DERIVATION. get_mip_va is, in full:
 *
 *     mip_offset = 0x200 - (1 << (9 - mip));          // 0, 256, 384, 448, ... 510
 *     return sample_data_va + ((tape_length_samples * mip_offset) >> 8);
 *
 * Consecutive bases therefore differ by tape/2^m, so mip m is the original decimated by 2^m
 * and the whole pyramid totals 2*tape — which is exactly what TAPE_LENGTH_SAMPLES "including
 * mips" means. Hence sample i of mip 0 is sample i >> m of mip m, and a slice at [off, off+n)
 * is at [off >> m, (off + n) >> m) relative to base(m).
 *
 * WHY THE OFFSETS ARE RELATIVE. get_mip_va(p, 0, false) reduces to exactly sample_data_va, so
 * existing working playback computes sample_data_va + start_read_only. The device reports
 * sample_data_va = 8421376, i.e. non-zero — so if start_read_only were an absolute virtual
 * address that sum would land 8.4M samples past the real data and breaks would be noise.
 * They are not. The offsets are relative. Measured 2026-07-26.
 *
 * RESIDENCY. The docs warn that Plinky "will try to load only the needed parts of each bank's
 * sample data", so the pyramid might not have been paged in at all. Probed on device: every
 * mip from 0 to 8 came back with peak amplitudes comparable to mip 0. It is resident.
 *
 * mipmap_test.c pins all of this to the numbers the device actually printed.
 */
#ifndef AMBIOTICA_MIPMAP_H
#define AMBIOTICA_MIPMAP_H

/* Base virtual address of mip m. Mirrors the SDK's get_mip_va, including its 32-bit
   arithmetic: at the largest legal tape (2<<21) and the largest offset (510) the product is
   2139095040, which fits in uint32 — but only just, which mipmap_test.c asserts. */
static unsigned int mip_base(unsigned int sample_data_va, unsigned int tape_length, unsigned int m) {
    unsigned int mip_offset = 0x200u - (1u << (9u - m));
    return sample_data_va + ((tape_length * mip_offset) >> 8);
}

/* Virtual address of a slice offset within mip m. */
static unsigned int mip_va(unsigned int sample_data_va, unsigned int tape_length,
                           unsigned int offset, unsigned int m) {
    return mip_base(sample_data_va, tape_length, m) + (offset >> m);
}

/* How many samples a mip-0 length becomes in mip m. */
static unsigned int mip_len(unsigned int len, unsigned int m) { return len >> m; }

/* Which mip to read at a given playback rate: floor(log2(rate)), clamped to the 9 that exist.
   Pitching DOWN cannot alias, so anything at or below unity stays on the unfiltered original.
   Reading mip m at rate/2^m gives the same pitch with the aliasing already filtered out —
   and when the rate is an exact power of two this lands on rate 1.0, which the renderer's
   integer fast path picks up for free. */
static unsigned int mip_for_rate(float rate) {
    unsigned int m = 0;
    while (rate >= 2.0f && m < 8u) { rate *= 0.5f; m++; }
    return m;
}

#endif
