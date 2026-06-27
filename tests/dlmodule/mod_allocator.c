/*
 * mod_allocator.c — Memory allocator with complex relocation
 *
 * Uses kernel exports: rt_malloc, rt_free, rt_calloc, rt_kprintf
 * Relocation types: JUMP_SLOT (×3 kernel functions), GLOB_DAT, RELATIVE
 *
 * Build: aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2
 *        -o mod_allocator.so mod_allocator.c -Wl,-e,0
 */

/* ── External kernel symbols ── */
extern void *rt_malloc(unsigned long size);
extern void  rt_free(void *ptr);
extern void *rt_calloc(unsigned long count, unsigned long size);
extern int   rt_kprintf(const char *fmt, ...);
#ifndef NULL
#define NULL ((void*)0)
#endif

/* ── Module data ── */
#define MAX_ALLOCS 32

typedef struct {
    void         *ptr;
    unsigned long size;
    int           active;
    const char   *tag;
} alloc_entry_t;

static alloc_entry_t alloc_table[MAX_ALLOCS];     /* BSS — no relocation needed */
static int            alloc_count = 0;
static unsigned long  total_allocated = 0;        /* GLOB_DAT in .data */
static unsigned long  total_freed = 0;            /* GLOB_DAT in .data */
static unsigned long  peak_bytes = 0;             /* GLOB_DAT in .data */

/* Static tag strings — RELATIVE for pointers to them */
static const char *default_tag = "default";

/* ── Public API ── */

void *mod_malloc(unsigned long size, const char *tag)
{
    if (size == 0) return NULL;
    if (alloc_count >= MAX_ALLOCS) return NULL;

    void *p = rt_malloc(size);                     /* JUMP_SLOT → rt_malloc */
    if (!p) return NULL;

    alloc_table[alloc_count].ptr    = p;
    alloc_table[alloc_count].size   = size;
    alloc_table[alloc_count].active = 1;
    alloc_table[alloc_count].tag    = tag ? tag : default_tag;  /* RELATIVE */
    alloc_count++;
    total_allocated += size;
    if (total_allocated - total_freed > peak_bytes)
        peak_bytes = total_allocated - total_freed;

    return p;
}

void *mod_calloc(unsigned long count, unsigned long size, const char *tag)
{
    if (count == 0 || size == 0) return NULL;
    if (alloc_count >= MAX_ALLOCS) return NULL;

    void *p = rt_calloc(count, size);              /* JUMP_SLOT → rt_calloc */
    if (!p) return NULL;

    alloc_table[alloc_count].ptr    = p;
    alloc_table[alloc_count].size   = count * size;
    alloc_table[alloc_count].active = 1;
    alloc_table[alloc_count].tag    = tag ? tag : default_tag;
    alloc_count++;
    total_allocated += count * size;
    if (total_allocated - total_freed > peak_bytes)
        peak_bytes = total_allocated - total_freed;

    return p;
}

int mod_free(void *ptr)
{
    if (!ptr) return -1;

    for (int i = 0; i < alloc_count; i++) {
        if (alloc_table[i].ptr == ptr && alloc_table[i].active) {
            rt_free(ptr);                          /* JUMP_SLOT → rt_free */
            alloc_table[i].active = 0;
            total_freed += alloc_table[i].size;
            return 0;
        }
    }
    return -2; /* not found */
}

void mod_free_all(void)
{
    for (int i = 0; i < alloc_count; i++) {
        if (alloc_table[i].active) {
            rt_free(alloc_table[i].ptr);           /* JUMP_SLOT → rt_free */
            total_freed += alloc_table[i].size;
            alloc_table[i].active = 0;
        }
    }
}

void get_memory_stats(unsigned long *alloced, unsigned long *freed,
                      unsigned long *peak, int *count)
{
    if (alloced) *alloced = total_allocated;
    if (freed)   *freed   = total_freed;
    if (peak)    *peak    = peak_bytes;
    if (count)   *count   = alloc_count;

    /* Read GLOB_DAT-fixed globals */
    rt_kprintf("[allocator] alloc=%lu freed=%lu peak=%lu count=%d\n",
               total_allocated, total_freed, peak_bytes, alloc_count);
}

int module_init(void)
{
    /* Zero all — BSS */
    for (int i = 0; i < MAX_ALLOCS; i++) {
        alloc_table[i].ptr    = NULL;
        alloc_table[i].size   = 0;
        alloc_table[i].active = 0;
        alloc_table[i].tag    = NULL;
    }
    alloc_count     = 0;
    total_allocated = 0;
    total_freed     = 0;
    peak_bytes      = 0;
    rt_kprintf("[allocator] module loaded, max_allocs=%d\n", MAX_ALLOCS);
    return 0;
}

void module_cleanup(void)
{
    mod_free_all();
    rt_kprintf("[allocator] module unloaded, freed all\n");
}
