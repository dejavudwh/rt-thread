/*
 * mod_relr_test.c — 触发 RELR 压缩重定位（.relr.dyn）
 *
 * 回归目标：BUGS.md #1。用 `-z pack-relative-relocs` 打包后，函数指针表的
 * RELATIVE 重定位会被折叠进 .relr.dyn 位图，走 dlelf.c 的 RELR 解码路径。
 * 若 loader 未处理 RELR，op_table 里的指针停留在 0 附近的文件内偏移，调用即
 * 跳空崩溃（EPC 指向垃圾地址）。
 *
 * 关键点：op_table 必须是**全局（非 static）**对象。static 表在 -O2 下会被
 * 编译器常量折叠成对 op_add/op_mul 的直接调用，从而不产生任何 RELATIVE，
 * 也就没有 RELR。全局表会导出并保留在 .data.rel.ro，强制产生 3 个 RELATIVE。
 *
 * 构建（见 build.sh）：
 *   aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2 \
 *       -Wl,-z,pack-relative-relocs -o mod_relr.so mod_relr_test.c
 */

#ifndef NULL
#define NULL ((void*)0)
#endif

static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }

typedef int (*binop_t)(int, int);

/* 全局函数指针表：地址依赖加载基址 → 3× R_AARCH64_RELATIVE → 打包进 RELR */
binop_t op_table[] = { op_add, op_sub, op_mul };

/* 通过表间接调用：强制读取已被 RELR 重定位的函数指针 */
int relr_add(int a, int b) { return op_table[0](a, b); }
int relr_sub(int a, int b) { return op_table[1](a, b); }
int relr_mul(int a, int b) { return op_table[2](a, b); }

int module_init(void) { return 0; }
void module_cleanup(void) {}
