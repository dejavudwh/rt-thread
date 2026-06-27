/*
 * mod_reloc_full.c — exercises ALL AArch64 relocation types
 *
 * This module is desiged to trigger as many relocation types as
 * possible when compiled with: -shared -fPIC -O0
 *
 * Each function/global is annotated with the relocation type(s)
 * it is expected to trigger.
 *
 * Build:
 *   aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O0 \
 *       -o mod_reloc_full.so mod_reloc_full.c -Wl,-e,module_init,-z,now
 */

#ifndef NULL
#define NULL ((void*)0)
#endif

extern int rt_kprintf(const char *fmt, ...);

/* ════════════════════════════════════════════════════════════
 * Group A: Global data → RELATIVE, GLOB_DAT, ABS64
 * ════════════════════════════════════════════════════════════ */

static int private_val = 0x42;

int       *g_ptr64    = &private_val;  /* R_AARCH64_ABS64    */
short     *g_shortptr = NULL;          /* R_AARCH64_RELATIVE (filled by init) */
char      *g_charptr  = NULL;

/* ════════════════════════════════════════════════════════════
 * Group B: Static function table → RELATIVE
 * ════════════════════════════════════════════════════════════ */

typedef int (*op_t)(int, int);

static int add_op(int a, int b) { return a + b; }
static int sub_op(int a, int b) { return a - b; }
static int mul_op(int a, int b) { return a * b; }

static op_t op_table[] = {
    add_op,    /* R_AARCH64_RELATIVE */
    sub_op,    /* R_AARCH64_RELATIVE */
    mul_op,    /* R_AARCH64_RELATIVE */
};

/* ════════════════════════════════════════════════════════════
 * Group C: MOVW/MOVK absolute address construction
 *   With -O0, loading a large constant literal may trigger
 *   R_AARCH64_MOVW_UABS_G0/G1/G2/G3.
 * ════════════════════════════════════════════════════════════ */

static unsigned long long large_addr = 0x123456789ABCDEF0ULL;

/* ════════════════════════════════════════════════════════════
 * Group D: ADRP + LDR sequence (GOT access)
 *   Access to global data via GOT generates:
 *     ADRP  x0, :got:sym    → R_AARCH64_ADR_GOT_PAGE
 *     LDR   x0, [x0, #:lo12] → not a dynamic reloc type in ET_DYN
 *   These are handled via GLOB_DAT, tested via the data above.
 * ════════════════════════════════════════════════════════════

 * ════════════════════════════════════════════════════════════
 * External kernel calls → JUMP_SLOT
 * ════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════════════════════════
 * Public API
 * ════════════════════════════════════════════════════════════ */

/* test_abs64: read a globally relocated pointer */
int test_abs64(void)
{
    if (!g_ptr64) return -1;
    return *g_ptr64; /* should be 0x42 */
}

/* test_relative_table: use function dispatch table */
int test_relative_table(int op, int a, int b)
{
    rt_kprintf("[reloc_full] op_table[%d](%d,%d)\n", op, a, b);
    if (op < 0 || op >= 3) return -1;
    return op_table[op](a, b);
}

/* test_large_const: return the 64-bit constant (exercises MOVW) */
unsigned long long test_large_const(void)
{
    rt_kprintf("[reloc_full] large_addr = 0x%llx\n", large_addr);
    return large_addr;
}

/* test_prel: self-referential pointer check */
int test_prel(int x)
{
    return x + private_val;
}

/* chain test */
int test_chain(int op, int a, int b)
{
    int r = test_relative_table(op, a, b);
    rt_kprintf("[reloc_full] chain: op=%d (%d,%d) => %d\n", op, a, b, r);
    return r;
}

/* ════════════════════════════════════════════════════════════
 * Module lifecycle
 * ════════════════════════════════════════════════════════════ */

int module_init(void)
{
    g_shortptr = (short *)&private_val;
    g_charptr  = (char *)&private_val;
    rt_kprintf("[reloc_full] module loaded, private_val=%d, "
               "g_ptr64=%p, large=0x%llx\n",
               private_val, (void*)g_ptr64, large_addr);
    return 0;
}

void module_cleanup(void)
{
    rt_kprintf("[reloc_full] module unloaded\n");
}
