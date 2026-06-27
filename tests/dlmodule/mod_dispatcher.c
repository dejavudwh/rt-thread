/*
 * mod_dispatcher.c — Event dispatcher (ALL relocation types)
 *
 * Most complex module — exercises every supported relocation type:
 *   R_AARCH64_NONE      (0)    — from zero-initialized data pointers
 *   R_AARCH64_ABS64     (257)  — absolute pointer tables
 *   R_AARCH64_GLOB_DAT  (1025) — GOT entries for global data/extern
 *   R_AARCH64_JUMP_SLOT (1026) — PLT entries for kernel function calls
 *   R_AARCH64_RELATIVE  (1027) — static callback table pointers
 *
 * Uses kernel exports: rt_kprintf, strlen, strcmp, memcpy, memset
 *
 * Build: aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O1
 *        -o mod_dispatcher.so mod_dispatcher.c -Wl,-e,0
 */

/* ── External kernel symbols ── */
extern int   rt_kprintf(const char *fmt, ...);
extern unsigned long strlen(const char *s);
extern int   strcmp(const char *a, const char *b);
extern void *memcpy(void *d, const void *s, unsigned long n);
extern void *memset(void *s, int c, unsigned long n);
#ifndef NULL
#define NULL ((void*)0)
#endif

/* ── Event system types ── */
#define MAX_HANDLERS    16
#define MAX_EVENT_NAME  32

typedef enum {
    EVENT_NONE = 0,
    EVENT_INIT,
    EVENT_START,
    EVENT_STOP,
    EVENT_PAUSE,
    EVENT_RESUME,
    EVENT_ERROR,
    EVENT_DATA,
    EVENT_TIMEOUT,
    EVENT_CUSTOM,
} event_type_t;

typedef struct {
    event_type_t  type;
    int           priority;
    const char   *name;
    void         *data;
    int           data_len;
} event_t;

typedef int (*event_handler_t)(event_t *evt);

/* ── Static handler functions → R_AARCH64_RELATIVE ── */
static int h_init(event_t *e)   { rt_kprintf("  init(%s)\n", e->name);    return 0; }
static int h_start(event_t *e)  { rt_kprintf("  start(%s)\n", e->name);   return 0; }
static int h_stop(event_t *e)   { rt_kprintf("  stop(%s)\n", e->name);    return 0; }
static int h_error(event_t *e)  { rt_kprintf("  error(%s)!\n", e->name);  return -1; }
static int h_data(event_t *e)   { rt_kprintf("  data(%s,%d)\n", e->name, e->data_len); return e->data_len; }
static int h_default(event_t *e){ rt_kprintf("  default(%s)\n", e->name); return 0; }

/* ── Handler dispatch table → R_AARCH64_RELATIVE (×6 entries) ── */
static event_handler_t handler_table[] = {
    [EVENT_INIT]    = h_init,
    [EVENT_START]   = h_start,
    [EVENT_STOP]    = h_stop,
    [EVENT_ERROR]   = h_error,
    [EVENT_DATA]    = h_data,
    [EVENT_PAUSE]   = h_default,
    [EVENT_RESUME]  = h_default,
    [EVENT_TIMEOUT] = h_default,
    [EVENT_CUSTOM]  = h_default,
};

/* ── Global data → GLOB_DAT ── */
static int events_processed  = 0;
static int events_succeeded  = 0;
static int events_failed     = 0;

/* Static string table → RELATIVE pointers for .rodata refs */
static const char *event_type_names[] = {
    [EVENT_NONE]    = "NONE",
    [EVENT_INIT]    = "INIT",
    [EVENT_START]   = "START",
    [EVENT_STOP]    = "STOP",
    [EVENT_PAUSE]   = "PAUSE",
    [EVENT_RESUME]  = "RESUME",
    [EVENT_ERROR]   = "ERROR",
    [EVENT_DATA]    = "DATA",
    [EVENT_TIMEOUT] = "TIMEOUT",
    [EVENT_CUSTOM]  = "CUSTOM",
};

/* Config data → GLOB_DAT + ABS64 for static pointers ── */
static event_t system_events[] = {
    { EVENT_INIT,  1, "system.boot",   NULL, 0 },
    { EVENT_START, 2, "system.start",  NULL, 0 },
    { EVENT_ERROR, 0, "system.panic",  NULL, 0 },
};

/* ── Public API ── */

const char *event_type_str(event_type_t t)
{
    if (t >= EVENT_NONE && t <= EVENT_CUSTOM)
        return event_type_names[t];              /* RELATIVE for static array */
    return "UNKNOWN";
}

int dispatch_event(event_type_t type, const char *name,
                   void *data, int data_len)
{
    event_t evt;
    int     result = 0;

    memset(&evt, 0, sizeof(evt));
    evt.type     = type;
    evt.priority = 1;
    evt.name     = name;
    evt.data     = data;
    evt.data_len = data_len;

    events_processed++;

    if (type > EVENT_NONE && type <= EVENT_CUSTOM) {
        event_handler_t handler = handler_table[type];  /* RELATIVE */
        if (handler) {
            result = handler(&evt);                     /* indirect call */
            if (result < 0) events_failed++;
            else            events_succeeded++;
        }
    } else {
        events_failed++;
        result = -1;
    }

    return result;
}

int dispatch_system_event(event_type_t type)
{
    for (int i = 0; i < 3; i++) {
        if (system_events[i].type == type) {
            return dispatch_event(
                system_events[i].type,
                system_events[i].name,
                system_events[i].data,
                system_events[i].data_len
            );
        }
    }
    return -1;
}

int find_handler(const char *name)
{
    if (!name) return -1;
    unsigned long len = strlen(name);             /* JUMP_SLOT → strlen */

    for (int i = EVENT_INIT; i <= EVENT_CUSTOM; i++) {
        const char *n = event_type_names[i];
        if (n && strcmp(n, name) == 0)             /* JUMP_SLOT → strcmp */
            return i;
    }
    (void)len;
    return -1;
}

void get_event_stats(int *total, int *ok, int *failed)
{
    if (total)  *total  = events_processed;
    if (ok)     *ok     = events_succeeded;
    if (failed) *failed = events_failed;
}

int module_init(void)
{
    events_processed = 0;
    events_succeeded = 0;
    events_failed    = 0;

    rt_kprintf("[dispatcher] module loaded, %d handlers, %d events\n",
               (int)(sizeof(handler_table)/sizeof(handler_table[0])),
               (int)(sizeof(system_events)/sizeof(system_events[0])));
    return 0;
}
