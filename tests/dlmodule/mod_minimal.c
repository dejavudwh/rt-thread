/* Minimal .so — no external calls, just pure computation */
#ifndef NULL
#define NULL ((void*)0)
#endif

int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }

int module_init(void)
{
    int x = add(10, 20);       /* internal call, no PLT needed */
    int y = mul(x, 3);         /* internal call */
    return y;                  /* returns 90 */
}
