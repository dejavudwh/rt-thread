/* Test multiple external calls incrementally */
#ifndef NULL
#define NULL ((void*)0)
#endif

extern int rt_kprintf(const char *fmt, ...);
extern void *memcpy(void *dest, const void *src, unsigned long n);
extern unsigned long strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);

int module_init(void)
{
    char buf[32];

    rt_kprintf("[ext2] testing external calls...\n");

    /* test strlen */
    const char *msg = "hello";
    unsigned long len = strlen(msg);
    rt_kprintf("[ext2] strlen(\"%s\") = %lu\n", msg, len);

    /* test memcpy */
    memcpy(buf, msg, len + 1);
    rt_kprintf("[ext2] memcpy: buf=\"%s\"\n", buf);

    /* test strcmp */
    int r = strcmp("abc", "xyz");
    rt_kprintf("[ext2] strcmp(\"abc\",\"xyz\") = %d\n", r);

    int r2 = strcmp("same", "same");
    rt_kprintf("[ext2] strcmp(\"same\",\"same\") = %d\n", r2);

    return (int)len;
}
