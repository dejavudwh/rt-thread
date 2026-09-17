/*
 * mod_kapi.c — module that consumes the complex kernel export surface
 *            (bsp/qemu-virt64-aarch64/applications/kernel_exports.c)
 *
 * Exercises every cross-boundary access pattern on AArch64 dlmodule:
 *   - GLOB_DAT:  writable global (kapi_state/kapi_calls), const LUT (kapi_lut),
 *                function-pointer struct (kapi_ops)
 *   - JUMP_SLOT: rich-signature functions (kapi_init/transform/alloc/free),
 *                IPC accessors (kapi_ipc_*)
 *   - fn-ptr both ways: module reads kapi_ops members and calls them
 *                (kernel -> module), and registers kapi_cb which the kernel
 *                calls back (module -> kernel -> module)
 *
 * Build (see build.sh):
 *   aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2 \
 *       -o mod_kapi.so mod_kapi.c
 */

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ── External kernel symbols (resolved via dlmodule_symbol_find at load) ── */
extern int   rt_kprintf(const char *fmt, ...);
extern void *rt_malloc(unsigned long sz);
extern void  rt_free(void *ptr);

/* ── Mirror of the kernel-side ABI structs (must match kernel_exports.c) ── */
typedef struct
{
    int         id;
    const char *name;
    int         gain;
} kapi_ent_t;

typedef struct
{
    int  (*open)(int id);
    int  (*read)(int id, void *buf, int len);
    int  (*write)(int id, const void *buf, int len);
    void (*close)(int id);
} kapi_ops_t;

/* data symbols */
extern int              kapi_state;
extern int              kapi_calls;
extern const kapi_ent_t kapi_lut[8];
extern kapi_ops_t       kapi_ops;

/* functions */
extern int   kapi_init(int channels);
extern int   kapi_transform(const void *in, void *out, int len, int scale);
extern void *kapi_alloc(int size);
extern void  kapi_free(void *p);
extern int   kapi_ipc_init(void);
extern int   kapi_ipc_signal(void);
extern int   kapi_ipc_wait(void);
extern void  kapi_set_event_cb(void (*cb)(int));
extern int   kapi_fire_event(int code);

/* ── Callback the kernel will invoke (module -> kernel -> module) ── */
static int cb_last_code = -1;

static void kapi_cb(int code)
{
    cb_last_code = code;
    rt_kprintf("[mod_kapi] callback fired with code=%d\n", code);
}

/* ── Full end-to-end exercise; returns 0 on success, <0 on failure ── */
int kapi_run(void)
{
    int            rc = 0;
    int            i;
    unsigned char  in[8], out[8];
    kapi_ops_t    *ops = &kapi_ops;     /* GLOB_DAT: address of kernel struct */

    /* 1. writable data symbol: read initial value */
    if (kapi_state != 0xA5A5A5A5)
    {
        rt_kprintf("[mod_kapi] FAIL: kapi_state=%08x\n", kapi_state);
        return -1;
    }
    rt_kprintf("[mod_kapi] kapi_state initial = 0x%08x (ok)\n", kapi_state);

    /* 2. writable data symbol: mutate it */
    kapi_state = 0x12345678;
    rt_kprintf("[mod_kapi] kapi_state <- 0x%08x\n", kapi_state);

    /* 3. const LUT: read a couple of entries (GLOB_DAT) */
    if (kapi_lut[2].gain != 50 || kapi_lut[7].gain != 400)
    {
        rt_kprintf("[mod_kapi] FAIL: lut gains %d/%d\n",
                   kapi_lut[2].gain, kapi_lut[7].gain);
        return -2;
    }
    rt_kprintf("[mod_kapi] kapi_lut[2]={%s,%d%%} kapi_lut[7]={%s,%d%%} (ok)\n",
               kapi_lut[2].name, kapi_lut[2].gain,
               kapi_lut[7].name, kapi_lut[7].gain);

    /* 4. rich-signature fn (JUMP_SLOT) */
    if (kapi_init(8) != 8)
    {
        rt_kprintf("[mod_kapi] FAIL: kapi_init\n");
        return -3;
    }

    /* 5. function-pointer struct: call open/read/close indirectly */
    ops->open(1);
    if (ops->read(5, in, sizeof(in)) != (int)sizeof(in))
    {
        rt_kprintf("[mod_kapi] FAIL: ops->read\n");
        return -4;
    }
    rt_kprintf("[mod_kapi] ops->read(5) -> %d bytes (ok)\n", (int)sizeof(in));
    ops->close(1);

    /* 6. transform + alloc/free (JUMP_SLOT) */
    for (i = 0; i < (int)sizeof(in); i++)
        in[i] = (unsigned char)(100 + i);
    kapi_transform(in, out, (int)sizeof(in), 200);   /* 2x, clamped to 255 */
    if (out[0] != 200 || out[6] != 212)
    {
        rt_kprintf("[mod_kapi] FAIL: transform out[0]=%d out[6]=%d\n",
                   out[0], out[6]);
        return -5;
    }
    rt_kprintf("[mod_kapi] transform(2x) -> out[0]=%d out[6]=%d, calls=%d (ok)\n",
               out[0], out[6], kapi_calls);
    {
        void *p = kapi_alloc(64);
        if (p == NULL) { rt_kprintf("[mod_kapi] FAIL: alloc\n"); return -6; }
        kapi_free(p);
    }

    /* 7. IPC: acquire the kernel semaphore, then release it */
    if (kapi_ipc_init() != 0)
    {
        rt_kprintf("[mod_kapi] FAIL: ipc_init\n");
        return -7;
    }
    if (kapi_ipc_wait() != 0)        /* first take succeeds (value starts at 1) */
    {
        rt_kprintf("[mod_kapi] FAIL: ipc_wait first\n");
        return -8;
    }
    if (kapi_ipc_signal() != 0)      /* give it back */
    {
        rt_kprintf("[mod_kapi] FAIL: ipc_signal\n");
        return -9;
    }
    rt_kprintf("[mod_kapi] semaphore take/release round-trip (ok)\n");

    /* 8. callback: kernel fires the event into our callback */
    if (kapi_fire_event(42) != 0)
    {
        rt_kprintf("[mod_kapi] FAIL: fire_event (no cb?)\n");
        return -10;
    }
    if (cb_last_code != 42)
    {
        rt_kprintf("[mod_kapi] FAIL: cb_last_code=%d\n", cb_last_code);
        return -11;
    }

    rt_kprintf("[mod_kapi] ALL CHECKS PASSED\n");
    return rc;
}

/* ── Lifecycle ── */
int module_init(void)
{
    /* register our callback into the kernel before anything else */
    kapi_set_event_cb(kapi_cb);
    rt_kprintf("[mod_kapi] module_init: callback registered\n");
    return 0;
}

void module_cleanup(void)
{
    rt_kprintf("[mod_kapi] module_cleanup\n");
}
