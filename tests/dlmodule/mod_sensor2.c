/* mod_sensor2 — no double types, simplified */
#ifndef NULL
#define NULL ((void*)0)
#endif

extern int rt_kprintf(const char *fmt, ...);

typedef int (*filter_fn_t)(int raw);

static int filter_none(int v)       { return v; }
static int filter_lowpass(int v)    { return (v * 7 + 3) / 10; }
static int filter_median3(int v)    { return (v + v/2 + v/3) / 3; }
static int filter_clamp(int v)      { return v > 1000 ? 1000 : (v < 0 ? 0 : v); }
static int filter_amplify(int v)    { return v * 2; }
static int filter_attenuate(int v)  { return v / 2; }

static filter_fn_t filter_table[] = {
    filter_none, filter_lowpass, filter_median3,
    filter_clamp, filter_amplify, filter_attenuate,
};
static int filter_count = 6;

int process_sample(int raw_value, int filter_index)
{
    if (filter_index >= 0 && filter_index < filter_count) {
        int r = filter_table[filter_index](raw_value);
        rt_kprintf("[sensor] filter[%d](%d) = %d\n", filter_index, raw_value, r);
        return r;
    }
    return raw_value;
}

int get_filter_count(void) { return filter_count; }

int module_init(void)
{
    rt_kprintf("[sensor2] module loaded, %d filters\n", filter_count);
    return 0;
}
