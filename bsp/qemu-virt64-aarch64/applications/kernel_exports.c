/*
 * kernel_exports.c — complex kernel symbol export surface for dlmodule testing
 *
 * Purpose:
 *   Provide a realistic "device driver + service" API surface that a loadable
 *   module (.so) consumes across the kernel boundary.  Unlike the handful of
 *   libc/kservice symbols in dlsyms.c, this surface deliberately spans every
 *   cross-boundary access pattern a real module uses:
 *
 *     1. Writable global data      (int kapi_state, kapi_calls)   -> GLOB_DAT
 *     2. Read-only lookup table    (const kapi_lut[8])            -> GLOB_DAT
 *     3. Function-pointer struct   (kapi_ops)                     -> GLOB_DAT
 *        whose members the module reads and then calls indirectly
 *     4. Rich-signature functions  (kapi_init/transform/alloc/free) -> JUMP_SLOT
 *     5. IPC object accessors      (semaphore take/release)       -> JUMP_SLOT
 *     6. Callback registration     (module passes a fn pointer in;
 *        the kernel calls it back)                                -> fn-ptr both ways
 *
 * Every public symbol here must be RTM_EXPORT'ed so dlmodule_symbol_find()
 * can resolve it at module load time (see components/libc/posix/libdl).
 *
 * Build: picked up automatically by applications/SConscript (Glob('*.c')).
 */

#include <rtthread.h>
#include <rtm.h>

/* ────────────────────────────────────────────────────────────────
 * 1. Writable global data (cross-boundary data symbol, -> GLOB_DAT)
 * ──────────────────────────────────────────────────────────────── */
int kapi_state = 0xA5A5A5A5;    /* initial value the module can read/verify */
int kapi_calls = 0;             /* incremented on every transform() call */

/* ────────────────────────────────────────────────────────────────
 * 2. Read-only lookup table (-> GLOB_DAT for the base pointer)
 * ──────────────────────────────────────────────────────────────── */
typedef struct
{
    int         id;
    const char *name;
    int         gain;           /* scaling gain, per-cent */
} kapi_ent_t;

const kapi_ent_t kapi_lut[8] =
{
    { 0, "ch0",  10 },
    { 1, "ch1",  25 },
    { 2, "ch2",  50 },
    { 3, "ch3",  75 },
    { 4, "ch4", 100 },
    { 5, "ch5", 150 },
    { 6, "ch6", 200 },
    { 7, "ch7", 400 },
};

/* ────────────────────────────────────────────────────────────────
 * 3. Function-pointer ops struct (module reads ptrs, calls them)
 * ──────────────────────────────────────────────────────────────── */
typedef struct
{
    int  (*open)(int id);
    int  (*read)(int id, void *buf, int len);
    int  (*write)(int id, const void *buf, int len);
    void (*close)(int id);
} kapi_ops_t;

static int kapi_open(int id)
{
    rt_kprintf("[kapi] open(id=%d)\n", id);
    return 0;
}

static int kapi_read(int id, void *buf, int len)
{
    int i;
    rt_uint8_t *p = (rt_uint8_t *)buf;
    for (i = 0; i < len; i++)
        p[i] = (rt_uint8_t)(id + i);
    return len;
}

static int kapi_write(int id, const void *buf, int len)
{
    (void)id;
    rt_kprintf("[kapi] write(%d bytes)\n", len);
    return len;
}

static void kapi_close(int id)
{
    rt_kprintf("[kapi] close(id=%d)\n", id);
}

kapi_ops_t kapi_ops =
{
    .open  = kapi_open,
    .read  = kapi_read,
    .write = kapi_write,
    .close = kapi_close,
};

/* ────────────────────────────────────────────────────────────────
 * 4. Rich-signature functions (-> JUMP_SLOT)
 * ──────────────────────────────────────────────────────────────── */
int kapi_init(int channels)
{
    rt_kprintf("[kapi] init: %d channels, lut[2]=%s(gain %d%%)\n",
               channels, kapi_lut[2].name, kapi_lut[2].gain);
    return channels;
}

/* scale each byte of `in` by the per-channel gain from kapi_lut */
int kapi_transform(const void *in, void *out, int len, int scale)
{
    int i;
    const rt_uint8_t *src = (const rt_uint8_t *)in;
    rt_uint8_t *dst = (rt_uint8_t *)out;

    for (i = 0; i < len; i++)
    {
        int v = (int)src[i] * scale / 100;
        if (v > 255) v = 255;
        dst[i] = (rt_uint8_t)v;
    }

    kapi_calls++;
    return len;
}

void *kapi_alloc(int size)
{
    return rt_malloc((rt_size_t)size);
}

void kapi_free(void *p)
{
    rt_free(p);
}

/* ────────────────────────────────────────────────────────────────
 * 5. IPC object accessors (module takes/releases a kernel semaphore)
 * ──────────────────────────────────────────────────────────────── */
static rt_sem_t kapi_sem = RT_NULL;

int kapi_ipc_init(void)
{
    kapi_sem = rt_sem_create("kapi_sem", 1, RT_IPC_FLAG_PRIO);
    if (kapi_sem == RT_NULL)
        return -1;
    return 0;
}

/* release: increments the semaphore (never fails at value 1 semantics) */
int kapi_ipc_signal(void)
{
    if (kapi_sem == RT_NULL)
        return -1;
    return (int)rt_sem_release(kapi_sem);
}

/* wait: non-blocking acquire (timeout 0); 0 == acquired, -RT_ETIMEOUT == busy */
int kapi_ipc_wait(void)
{
    if (kapi_sem == RT_NULL)
        return -1;
    return (int)rt_sem_take(kapi_sem, 0);
}

/* ────────────────────────────────────────────────────────────────
 * 6. Callback registration (module -> kernel function pointer)
 * ──────────────────────────────────────────────────────────────── */
static void (*kapi_event_cb)(int) = RT_NULL;

void kapi_set_event_cb(void (*cb)(int))
{
    kapi_event_cb = cb;
    rt_kprintf("[kapi] event callback registered @0x%p\n", (void *)cb);
}

int kapi_fire_event(int code)
{
    if (kapi_event_cb == RT_NULL)
        return -1;
    kapi_event_cb(code);            /* calls back INTO the module */
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Symbol export table
 * ──────────────────────────────────────────────────────────────── */
RTM_EXPORT(kapi_state);
RTM_EXPORT(kapi_calls);
RTM_EXPORT(kapi_lut);
RTM_EXPORT(kapi_ops);

RTM_EXPORT(kapi_init);
RTM_EXPORT(kapi_transform);
RTM_EXPORT(kapi_alloc);
RTM_EXPORT(kapi_free);

RTM_EXPORT(kapi_ipc_init);
RTM_EXPORT(kapi_ipc_signal);
RTM_EXPORT(kapi_ipc_wait);

RTM_EXPORT(kapi_set_event_cb);
RTM_EXPORT(kapi_fire_event);
