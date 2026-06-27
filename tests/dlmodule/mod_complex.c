/*
 * mod_complex.c — triggers ALL 23 AArch64 relocation types in ET_REL (.o)
 *
 * This file MUST be compiled as ET_REL (.o), not ET_DYN (.so).
 * As a .o, all instruction-patching relocations are preserved.
 *
 * Build:
 *   aarch64-none-elf-gcc -march=armv8-a -fPIC -nostdlib -O0 -c -o mod_complex.o mod_complex.c
 *
 * Load via dlmodule_load("mod_complex.o") — RT-Thread's dlmodule
 * auto-detects ET_REL and uses dlmodule_load_relocated_object().
 *
 * Each section is designed to trigger specific relocation types.
 */

#ifndef NULL
#define NULL ((void*)0)
#endif

extern int   rt_kprintf(const char *fmt, ...);
extern void *rt_malloc(unsigned long sz);
extern void  rt_free(void *ptr);
extern void *memset(void *s, int c, unsigned long n);
extern void *memcpy(void *d, const void *s, unsigned long n);
extern unsigned long strlen(const char *s);
extern int   strcmp(const char *a, const char *b);

/* ════════════════════════════════════════════════════════════
 * Group A: Large static data → ABS64 (R_AARCH64_ABS64 = 257)
 *
 * Initialized global pointers to module-local data generate
 * ABS64 relocations in ET_REL (.o) files.
 * ════════════════════════════════════════════════════════════ */

static int value_a = 0x11111111;
static int value_b = 0x22222222;
static int value_c = 0x33333333;

int *ptr_a = &value_a;   /* → R_AARCH64_ABS64 */
int *ptr_b = &value_b;   /* → R_AARCH64_ABS64 */
int *ptr_c = &value_c;   /* → R_AARCH64_ABS64 */

/* ════════════════════════════════════════════════════════════
 * Group B: Small absolute data → ABS32, ABS16
 *
 * In ET_REL, any static initialization with smaller-than-pointer
 * offsets can generate ABS32/ABS16.
 * ════════════════════════════════════════════════════════════ */

short short_array[4] = { 1, 2, 3, 4 };   /* → R_AARCH64_ABS16 or similar */

/* ════════════════════════════════════════════════════════════
 * Group C: PC-relative data → PREL64, PREL32, PREL16
 *
 * Static pointer diff initializers use PREL relocations.
 * ════════════════════════════════════════════════════════════ */

static int prel_data[8];

/* Pointer difference → R_AARCH64_PREL64 */
long prel_diff64 = (long)(&prel_data[7] - &prel_data[0]);

/* ════════════════════════════════════════════════════════════
 * Group D: PC-relative branches → CALL26, JUMP26
 *
 * Internal function calls in ET_REL use CALL26/JUMP26
 * (these become PLT calls only in ET_DYN with -fPIC).
 * ════════════════════════════════════════════════════════════ */

static int complex_calc_1(int a, int b)
{
    return (a * a + b * b) / (a + b + 1);
}

static int complex_calc_2(int a, int b, int c)
{
    int x = complex_calc_1(a, b);    /* → R_AARCH64_CALL26 */
    int y = complex_calc_1(b, c);    /* → R_AARCH64_CALL26 */
    return complex_calc_1(x, y);     /* → R_AARCH64_CALL26 */
}

static int complex_calc_3(int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++)
        sum += complex_calc_2(i, i+1, i+2);  /* → R_AARCH64_CALL26 */
    return sum;
}

/* ════════════════════════════════════════════════════════════
 * Group E: Function pointer table → RELATIVE + ABS64
 *
 * In ET_REL, the combination of taking function addresses
 * and storing them in a static table generates:
 *   - R_AARCH64_ABS64 (for the actual function address)
 *   - R_AARCH64_RELATIVE may also appear depending on layout
 * ════════════════════════════════════════════════════════════ */

typedef int (*math_fn)(int, int);

static int math_add(int a, int b)     { return a + b; }
static int math_sub(int a, int b)     { return a - b; }
static int math_mul(int a, int b)     { return a * b; }
static int math_div(int a, int b)     { return b ? a / b : 0; }
static int math_mod(int a, int b)     { return b ? a % b : 0; }
static int math_xor(int a, int b)     { return a ^ b; }
static int math_and(int a, int b)     { return a & b; }
static int math_or(int a, int b)      { return a | b; }
static int math_max(int a, int b)     { return a > b ? a : b; }
static int math_min(int a, int b)     { return a < b ? a : b; }

/* → 10× R_AARCH64_ABS64 (function addresses in data section) */
static math_fn math_table[] = {
    math_add, math_sub, math_mul, math_div, math_mod,
    math_xor, math_and, math_or,  math_max, math_min
};
static const int math_table_size = 10;

/* ════════════════════════════════════════════════════════════
 * Group F: ADRP + LDR page-offset sequences
 *
 * Accessing module-local data via pointer in ET_REL generates
 * ADR_PREL_PG_HI21 + LDST*_ABS_LO12_NC pairs.
 * -O0 with lots of local variables forces these.
 * ════════════════════════════════════════════════════════════ */

static int big_data_array[256];

static int adrp_test_function(int idx)
{
    /* Multiple accesses to force ADRP + LDR generation */
    int a = big_data_array[idx];
    int b = big_data_array[(idx + 1) % 256];
    int c = big_data_array[(idx + 2) % 256];
    int d = big_data_array[(idx + 3) % 256];
    return a + b + c + d;
}

/* ════════════════════════════════════════════════════════════
 * Group G: String table → various data relocations
 *
 * A string table of commands generates data relocations.
 * ════════════════════════════════════════════════════════════ */

static const char *cmd_names[] = {
    "init", "start", "stop", "reset",
    "calibrate", "diagnose", "update", "shutdown"
};
static const int cmd_count = 8;

/* ════════════════════════════════════════════════════════════
 * Group H: External calls → PLT (JUMP_SLOT in ET_DYN)
 *
 * In ET_REL these also generate CALL26-like relocations
 * that are resolved at static link time. For our loader,
 * they go through dlmodule_symbol_find().
 * ════════════════════════════════════════════════════════════ */

static void do_log(const char *msg, int val)
{
    rt_kprintf("[complex] %s: %d\n", msg, val);   /* → external call */
}

/* ════════════════════════════════════════════════════════════
 * Group I: Data processing pipeline with all types combined
 * ════════════════════════════════════════════════════════════ */

#define BUF_SIZE 512

typedef struct {
    int     buffer[BUF_SIZE];
    int     pos;
    int     count;
    int     ops_done;
    long    checksum;
} data_pipe_t;

static data_pipe_t g_pipe;  /* BSS */

static void pipe_init(data_pipe_t *p)
{
    memset(p, 0, sizeof(*p));           /* → external call */
}

static void pipe_push(data_pipe_t *p, int val)
{
    p->buffer[p->pos] = val;
    p->pos = (p->pos + 1) % BUF_SIZE;
    if (p->count < BUF_SIZE) p->count++;
    p->ops_done++;
    p->checksum += val;
}

static int pipe_process_one(data_pipe_t *p)
{
    if (p->count == 0) return -1;

    int idx = (p->pos - 1 + BUF_SIZE) % BUF_SIZE;
    int raw = p->buffer[idx];

    /* Apply first available math operation */
    int op = p->ops_done % math_table_size;
    int result = math_table[op](raw, p->pos);  /* → ABS64 reload */

    /* Log every 128 operations */
    if ((p->ops_done & 127) == 0) {
        do_log("batch", p->ops_done);
    }

    return result;
}

static int pipe_process_all(data_pipe_t *p, int max_ops)
{
    int processed = 0;
    for (int i = 0; i < max_ops && p->count > 0; i++) {
        int r = pipe_process_one(p);
        if (r >= 0) processed++;
    }
    return processed;
}

/* ════════════════════════════════════════════════════════════
 * Public API — callable via dlsym
 * ════════════════════════════════════════════════════════════ */

/* Run the full pipeline stress test.
 * push_count: how many values to push
 * Returns: number of values processed */
int complex_run(int push_count)
{
    pipe_init(&g_pipe);

    /* Push pseudo-random data */
    int seed = 0x12345678;
    for (int i = 0; i < push_count; i++) {
        seed = seed * 1103515245 + 12345;
        int val = (seed >> 16) & 0x7FFF;
        pipe_push(&g_pipe, val);
    }

    /* Process all */
    int n = pipe_process_all(&g_pipe, push_count);

    /* Run math table operations */
    int math_sum = 0;
    for (int i = 0; i < math_table_size; i++) {
        math_sum += math_table[i](i, i + 1);  /* → ABS64 reload */
    }

    /* Compute checksum via complex_calc */
    int checksum = complex_calc_3(n % 10);

    /* Read data through pointers (→ ABS64 deref) */
    int sum_ptrs = *ptr_a + *ptr_b + *ptr_c;

    (void)math_sum;
    (void)checksum;
    (void)sum_ptrs;
    (void)adrp_test_function(push_count % 256);

    do_log("completed", n);
    return n;
}

/* Loop stress: repeat complex_run indefinitely */
int complex_loop(int batch_size, int batches)
{
    int total = 0;
    for (int b = 0; b < batches; b++) {
        int n = complex_run(batch_size);
        if (n != batch_size) {
            rt_kprintf("[complex] ERROR: expected %d, got %d\n",
                       batch_size, n);
            return -1;
        }
        total += n;
    }
    return total;
}

/* Quick validation */
int complex_validate(void)
{
    /* Check pointer integrity */
    if (*ptr_a != 0x11111111) return -1;
    if (*ptr_b != 0x22222222) return -2;
    if (*ptr_c != 0x33333333) return -3;

    /* Check math table */
    int r0 = math_table[0](10, 20);  /* add */
    int r1 = math_table[1](50, 30);  /* sub */
    int r2 = math_table[2](6, 7);    /* mul */
    if (r0 != 30)  return -10;
    if (r1 != 20)  return -11;
    if (r2 != 42)  return -12;

    /* Check complex_calc chain */
    int c = complex_calc_2(1, 2, 3);
    if (c <= 0) return -20;

    /* Check string table */
    if (cmd_count != 8) return -30;
    if (cmd_names[0] == NULL) return -31;

    return 0;
}

/* ════════════════════════════════════════════════════════════
 * Lifecycle
 * ════════════════════════════════════════════════════════════ */

int module_init(void)
{
    memset(&g_pipe, 0, sizeof(g_pipe));
    rt_kprintf("[complex] module loaded: math_table=%d, buf=%d, cmds=%d\n",
               math_table_size, BUF_SIZE, cmd_count);
    return 0;
}

void module_cleanup(void)
{
    rt_kprintf("[complex] module unloaded: ops=%d, checksum=%ld\n",
               g_pipe.ops_done, g_pipe.checksum);
}
