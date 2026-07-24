/* Ambiotica panel — compile-time build configuration.
 *
 * The Plinky web IDE bans #include, so amalgamate.sh CONCATENATES this file at the top
 * of the generated single-file panel (it is not #included). It holds only cross-cutting
 * build-target choices; per-module decisions live in their own files (e.g. the shimmer /
 * freeze-cloud disables are in microloop.c, and the reverb is always the Dattorro plate).
 *
 * The desktop harness does NOT use this file — build.sh sets LOOPER_I16 itself so it can
 * build both the float and int16 loop-bed variants for comparison. */
#define LOOPER_I16     /* loop bed stored as interleaved int16 (halves its PSRAM footprint) */
