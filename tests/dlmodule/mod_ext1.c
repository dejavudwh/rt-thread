/* Single external call — test JUMP_SLOT for rt_kprintf */
#ifndef NULL
#define NULL ((void*)0)
#endif

extern int rt_kprintf(const char *fmt, ...);

int module_init(void)
{
    rt_kprintf("[ext1] Hello from loaded .so!\n");
    return 42;
}
