/*
 * mod_string_util.c — String utility module
 *
 * Uses kernel exports: memcpy, strlen, strcmp, memset, rt_kprintf
 * Relocation types: GLOB_DAT, JUMP_SLOT, ABS64
 *
 * Build: aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2
 *        -o mod_string_util.so mod_string_util.c -Wl,-e,0
 */

/* ── External kernel symbols (resolved at load time) ── */
extern void *memcpy(void *dest, const void *src, unsigned long n);
extern unsigned long strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern void *memset(void *s, int c, unsigned long n);
extern int rt_kprintf(const char *fmt, ...);

/* ── Module-private data (GLOB_DAT via GOT) ── */
static const char *module_name = "string_util_v1.0";
static int total_ops = 0;

typedef struct {
    const char *src;
    char       *dst;
    unsigned long len;
    int         result;
} string_op_t;

/* Allocated buffer pool */
static char strbuf[256];
static string_op_t last_op;

/* ── Public functions ── */

/* copy_string: uses memcpy + strlen (2 × JUMP_SLOT) */
int copy_string(const char *input)
{
    unsigned long len;
    if (!input) return -1;

    len = strlen(input);                          /* JUMP_SLOT → strlen */
    if (len >= sizeof(strbuf)) return -2;

    memcpy(strbuf, input, len + 1);               /* JUMP_SLOT → memcpy */
    strbuf[len] = '\0';

    last_op.src    = input;
    last_op.dst    = strbuf;
    last_op.len    = len;
    last_op.result = 0;
    total_ops++;

    return (int)len;
}

/* compare_strings: uses strcmp (JUMP_SLOT) */
int compare_strings(const char *a, const char *b)
{
    if (!a || !b) return -2;

    int r = strcmp(a, b);                         /* JUMP_SLOT → strcmp */
    last_op.result = r;
    total_ops++;
    return r;
}

/* zero_buffer: uses memset (JUMP_SLOT) */
void zero_buffer(void)
{
    memset(strbuf, 0, sizeof(strbuf));            /* JUMP_SLOT → memset */
    last_op.dst    = strbuf;
    last_op.len    = sizeof(strbuf);
    last_op.result = 0;
    total_ops++;
}

/* get_stats: reads module data via GLOB_DAT fixups */
int get_stats(char *name_out, int name_size)
{
    if (name_out && name_size > 0) {
        unsigned long n = strlen(module_name);    /* GLOB_DAT for module_name */
        if (n >= (unsigned long)name_size) n = (unsigned long)name_size - 1;
        memcpy(name_out, module_name, n);         /* GLOB_DAT for module_name */
        name_out[n] = '\0';
    }
    rt_kprintf("[%s] total_ops=%d\n", module_name, total_ops); /* 2 × GLOB_DAT */
    return total_ops;
}

int module_init(void)
{
    memset(strbuf, 0, sizeof(strbuf));
    memset(&last_op, 0, sizeof(last_op));
    total_ops = 0;
    rt_kprintf("[%s] module loaded\n", module_name);
    return 0;
}

void module_cleanup(void)
{
    rt_kprintf("[%s] module unloaded, ops=%d\n", module_name, total_ops);
    memset(strbuf, 0, sizeof(strbuf));
}
