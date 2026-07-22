/* Two-region arena allocator for the amalgamated Plinky panel.
 *
 * The vendored DSP allocates all its buffers via calloc() at create time. Plinky
 * panels have no heap, so in the single-file build we #define calloc -> this bump
 * allocator, which hands out either PSRAM (big sequential buffers) or a fixed
 * SRAM pool owned by the panel (small/hot state). The panel sets the target
 * region + bases before each *_create(). No runtime frees (arena lives for the
 * panel's lifetime). Placed FIRST in the amalgamation, before any DSP source.
 */
static int    g_amb_region = 0;                 /* 0 = SRAM pool, 1 = PSRAM */
static unsigned char* g_amb_sr_base = 0; static size_t g_amb_sr_cap = 0, g_amb_sr_used = 0;
static short*         g_amb_ps_base = 0; static size_t g_amb_ps_cap = 0, g_amb_ps_used = 0;

static void* panel_calloc(size_t n, size_t sz) {
    size_t bytes = (n * sz + 7u) & ~((size_t)7u);
    if (g_amb_region) {                          /* PSRAM */
        if (g_amb_ps_used + bytes > g_amb_ps_cap) return 0;
        void* p = (unsigned char*)g_amb_ps_base + g_amb_ps_used;
        g_amb_ps_used += bytes; memset(p, 0, bytes); return p;
    }
    if (g_amb_sr_used + bytes > g_amb_sr_cap) return 0;   /* SRAM */
    void* p = g_amb_sr_base + g_amb_sr_used;
    g_amb_sr_used += bytes; memset(p, 0, bytes); return p;
}
static void* panel_malloc(size_t sz)            { return panel_calloc(1, sz); }
static void  panel_free(void* p)                { (void)p; }
static void* panel_realloc(void* p, size_t sz)  { (void)p; return panel_calloc(1, sz); }

#define calloc  panel_calloc
#define malloc  panel_malloc
#define free    panel_free
#define realloc panel_realloc
