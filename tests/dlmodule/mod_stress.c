/*
 * mod_stress.c — complex real-world-style stress test module
 *
 * Mimics a realistic embedded data-processing pipeline:
 *   - Ring buffer with statistics
 *   - Multiple filter stages via function pointer dispatch (→ RELATIVE)
 *   - Lookup tables with static initializers (→ GLOB_DAT)
 *   - String table + command parser (→ JUMP_SLOT for kernel calls)
 *   - Memory pool allocator wrapping rt_malloc/rt_free
 *   - CRC-32 checksum (pure computation, internal calls → CALL26 with -O2)
 *   - Stress loop that runs all stages thousands of times
 *
 * Build:
 *   aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2 \
 *       -o mod_stress.so mod_stress.c -Wl,-e,module_init,-z,now
 */

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ── External kernel symbols ── */
extern int   rt_kprintf(const char *fmt, ...);
extern void *rt_malloc(unsigned long sz);
extern void  rt_free(void *ptr);
extern void *memset(void *s, int c, unsigned long n);
extern void *memcpy(void *d, const void *s, unsigned long n);
extern unsigned long strlen(const char *s);

/* ════════════════════════════════════════════════════════════
 * SECTION 1: Static lookup tables (→ GLOB_DAT, RELATIVE)
 * ════════════════════════════════════════════════════════════ */

/* CRC-32 lookup table — 256 entries, statically initialized */
static const unsigned long crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D3851F,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6B20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB30A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

/* Filter coefficient table (static, → GLOB_DAT for pointers) */
static const int fir_coeffs[8] = { 2, 4, 8, 16, 16, 8, 4, 2 };
static const int iir_fb[4]     = { 75, 50, 25, 0 };  /* feedback % */
static const char *filter_names[] = {
    "passthrough", "lowpass", "highpass", "bandpass",
    "notch", "moving_avg", "median", "fir8"
};

/* ════════════════════════════════════════════════════════════
 * SECTION 2: Function dispatch tables (→ RELATIVE)
 * ════════════════════════════════════════════════════════════ */

typedef int (*filter_fn)(int sample, void *ctx);
typedef int (*post_fn)(int value);

/* ── Filter implementations ── */
static int flt_passthrough(int s, void *c) { (void)c; return s; }
static int flt_lowpass(int s, void *c)
{
    int *state = (int *)c;
    *state = (*state * 70 + s * 30) / 100;
    return *state;
}
static int flt_highpass(int s, void *c)
{
    int *state = (int *)c;
    int out = (s * 120 - *state * 80) / 100;
    *state = (*state * 70 + s * 30) / 100;
    return out;
}
static int flt_bandpass(int s, void *c)
{
    int *state = (int *)c;
    int lp = (*state * 85 + s * 15) / 100;
    int hp = (s * 120 - *state * 80) / 100;
    *state = lp;
    return (lp + hp) / 2;
}
static int flt_moving_avg(int s, void *c)
{
    int *buf = (int *)c;
    int sum = 0;
    for (int i = 6; i > 0; i--) { buf[i] = buf[i-1]; sum += buf[i]; }
    buf[0] = s; sum += s;
    return sum / 8;
}
static int flt_median3(int s, void *c)
{
    int *buf = (int *)c;
    buf[2] = buf[1]; buf[1] = buf[0]; buf[0] = s;
    /* simple median-of-3 */
    int a = buf[0], b = buf[1], c_ = buf[2];
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c_) { int t = b; b = c_; c_ = t; }
    if (a > b) { int t = a; a = b; b = t; }
    return b;
}
static int flt_fir8(int s, void *c)
{
    int *buf = (int *)c;
    int sum = 0;
    for (int i = 7; i > 0; i--) { buf[i] = buf[i-1]; sum += buf[i] * fir_coeffs[i]; }
    buf[0] = s; sum += s * fir_coeffs[0];
    return sum / 60;
}

/* Dispatch table → R_AARCH64_RELATIVE ×8 */
static filter_fn filter_table[] = {
    flt_passthrough, flt_lowpass,    flt_highpass, flt_bandpass,
    flt_moving_avg,  flt_median3,    flt_fir8,      flt_median3
};
static const int filter_count = 8;

/* Post-processing table → R_AARCH64_RELATIVE ×4 */
static int post_none(int v)   { return v; }
static int post_abs(int v)    { return v < 0 ? -v : v; }
static int post_clamp(int v)  { return v > 10000 ? 10000 : (v < -10000 ? -10000 : v); }
static int post_scale10(int v){ return v / 10; }

static post_fn post_table[] = { post_none, post_abs, post_clamp, post_scale10 };
static const int post_count = 4;

/* ════════════════════════════════════════════════════════════
 * SECTION 3: Ring buffer with stats (→ GLOB_DAT for globals)
 * ════════════════════════════════════════════════════════════ */

#define RING_SIZE 1024

typedef struct {
    int     buffer[RING_SIZE];
    int     head;
    int     tail;
    int     count;
    int     total_samples;
    long    sum;
    long    sum_sq;
    int     min_val;
    int     max_val;
} ring_buf_t;

static ring_buf_t g_ring;  /* BSS — zero init */

static void ring_push(ring_buf_t *r, int val)
{
    r->buffer[r->head] = val;
    r->head = (r->head + 1) % RING_SIZE;
    if (r->count < RING_SIZE) {
        r->count++;
    } else {
        r->tail = (r->tail + 1) % RING_SIZE;
    }
    r->total_samples++;
    r->sum += val;
    r->sum_sq += (long)val * val;
    if (r->total_samples == 1 || val < r->min_val) r->min_val = val;
    if (r->total_samples == 1 || val > r->max_val) r->max_val = val;
}

/* ════════════════════════════════════════════════════════════
 * SECTION 4: Memory pool wrapper (→ JUMP_SLOT for rt_malloc/free)
 * ════════════════════════════════════════════════════════════ */

#define POOL_MAX 64

typedef struct {
    void         *ptr;
    unsigned long size;
    int           in_use;
} pool_entry_t;

static pool_entry_t g_pool[POOL_MAX];  /* BSS */
static int          g_pool_count;
static unsigned long g_pool_alloced;
static unsigned long g_pool_freed;
static unsigned long g_pool_peak;

/* Pipeline stage state */
struct pipeline_stage {
    int         filter_idx;
    int         post_idx;
    int         state[16];
    int         processed;
    int         dropped;
    long        latency_sum;
};
static struct pipeline_stage stages[4];
static int                   stages_initialized = 0;

static void *pool_alloc(unsigned long sz)
{
    if (g_pool_count >= POOL_MAX) return NULL;
    void *p = rt_malloc(sz);  /* → JUMP_SLOT */
    if (!p) return NULL;
    g_pool[g_pool_count].ptr  = p;
    g_pool[g_pool_count].size = sz;
    g_pool[g_pool_count].in_use = 1;
    g_pool_count++;
    g_pool_alloced += sz;
    if (g_pool_alloced - g_pool_freed > g_pool_peak)
        g_pool_peak = g_pool_alloced - g_pool_freed;
    return p;
}

static void pool_free(void *p)
{
    for (int i = 0; i < g_pool_count; i++) {
        if (g_pool[i].ptr == p && g_pool[i].in_use) {
            rt_free(p);      /* → JUMP_SLOT */
            g_pool[i].in_use = 0;
            g_pool_freed += g_pool[i].size;
            return;
        }
    }
}

static void pool_free_all(void)
{
    for (int i = 0; i < g_pool_count; i++) {
        if (g_pool[i].in_use) {
            rt_free(g_pool[i].ptr);
            g_pool_freed += g_pool[i].size;
            g_pool[i].in_use = 0;
        }
    }
}

/* ════════════════════════════════════════════════════════════
 * SECTION 5: CRC-32 (pure computation, internal calls)
 * ════════════════════════════════════════════════════════════ */

static unsigned long crc32(const unsigned char *data, unsigned long len)
{
    unsigned long crc = 0xFFFFFFFF;
    for (unsigned long i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* ════════════════════════════════════════════════════════════
 * SECTION 6: Data pipeline — single stage
 * (struct pipeline_stage defined at top of file)
 * ════════════════════════════════════════════════════════════ */

static int process_stage(struct pipeline_stage *st, int sample)
{
    if (st->filter_idx < 0 || st->filter_idx >= filter_count) return sample;

    filter_fn flt = filter_table[st->filter_idx];   /* → RELATIVE load */
    post_fn   pst = post_table[st->post_idx];        /* → RELATIVE load */

    int filtered = flt(sample, st->state);
    int result   = pst(filtered);

    st->processed++;
    if (result == 0 && sample != 0) st->dropped++;
    st->latency_sum += 1;  /* pretend 1-cycle latency */

    return result;
}

/* ════════════════════════════════════════════════════════════
 * SECTION 7: Public API
 * ════════════════════════════════════════════════════════════ */

/* stress_run(): runs the full pipeline for N iterations
 * Returns: number of samples processed successfully */
int stress_run(int iterations)
{
    int ok = 0;

    if (!stages_initialized) {
        for (int i = 0; i < 4; i++) {
            memset(&stages[i], 0, sizeof(stages[i]));
            stages[i].filter_idx = (i * 2) % filter_count;
            stages[i].post_idx   = i % post_count;
        }
        stages_initialized = 1;
    }

    /* Generate pseudo-random data and push through pipeline */
    int seed = 0xDEADBEEF;
    for (int n = 0; n < iterations; n++) {
        /* Simple LCG pseudo-random */
        seed = seed * 1103515245 + 12345;
        int sample = (seed >> 16) & 0x7FFF;

        /* Push to ring buffer */
        ring_push(&g_ring, sample);

        /* Run through pipeline stages */
        int val = sample;
        for (int s = 0; s < 4; s++) {
            val = process_stage(&stages[s], val);
        }

        /* Compute CRC of result as checksum verification */
        unsigned char tmp[4];
        tmp[0] = val & 0xFF;
        tmp[1] = (val >> 8) & 0xFF;
        tmp[2] = (val >> 16) & 0xFF;
        tmp[3] = (val >> 24) & 0xFF;
        unsigned long crc = crc32(tmp, 4);
        (void)crc;

        /* Allocate and free some memory every 100 iterations */
        if ((n % 100) == 0) {
            void *p1 = pool_alloc(64);
            void *p2 = pool_alloc(128);
            if (p1) pool_free(p1);
            if (p2) pool_free(p2);
        }

        ok++;
    }

    return ok;
}

/* get_stats(): fill statistics structure */
void stress_get_stats(int *total, int *processed, int *min, int *max,
                      long *sum, unsigned long *pool_alloc,
                      unsigned long *pool_peak, int *pool_count)
{
    int total_stages_processed = 0;
    for (int s = 0; s < 4; s++)
        total_stages_processed += stages[s].processed;

    if (total)        *total        = g_ring.total_samples;
    if (processed)    *processed    = total_stages_processed;
    if (min)          *min          = g_ring.min_val;
    if (max)          *max          = g_ring.max_val;
    if (sum)          *sum          = g_ring.sum;
    if (pool_alloc)   *pool_alloc   = g_pool_alloced;
    if (pool_peak)    *pool_peak    = g_pool_peak;
    if (pool_count)   *pool_count   = g_pool_count;
}

/* quick_check: lightweight validation without prints */
int stress_quick_check(void)
{
    /* Just verify internal consistency */
    if (filter_count != 8)  return -1;
    if (post_count != 4)    return -2;
    if (g_pool_count < 0)   return -3;
    return 0;
}

/* mem_test: allocate and free memory in patterns */
int stress_mem_test(int num_allocs)
{
    void *ptrs[32];
    int n = 0;

    if (num_allocs > 32) num_allocs = 32;
    for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = pool_alloc(128 + (i * 16));
        if (ptrs[i]) n++;
    }
    for (int i = 0; i < num_allocs; i++) {
        if (ptrs[i]) pool_free(ptrs[i]);
    }
    return n;
}

/* ════════════════════════════════════════════════════════════
 * Lifecycle
 * ════════════════════════════════════════════════════════════ */

int module_init(void)
{
    memset(&g_ring, 0, sizeof(g_ring));
    g_ring.min_val = 0x7FFFFFFF;
    g_ring.max_val = 0x80000000;

    memset(g_pool, 0, sizeof(g_pool));
    g_pool_count   = 0;
    g_pool_alloced = 0;
    g_pool_freed   = 0;
    g_pool_peak    = 0;

    rt_kprintf("[stress] module loaded: %d filters, %d post-processors, "
               "ring=%d, pool=%d\n",
               filter_count, post_count, RING_SIZE, POOL_MAX);
    return 0;
}

void module_cleanup(void)
{
    pool_free_all();
    rt_kprintf("[stress] module unloaded: pool freed %lu bytes\n",
               g_pool_freed);
}
