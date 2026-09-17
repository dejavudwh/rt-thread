# AArch64 dlmodule 使用指南

> 目标读者：想在 RT-Thread（裸机 / 无 RT-Smart）上**加载并运行 AArch64 动态模块（`.so`）**的开发者。
>
> 本文档以 `bsp/qemu-virt64-aarch64` 为平台，覆盖从「编译内核 → 编写/编译模块 →
> 打包 FAT → QEMU 运行 → 故障排查」的完整闭环，并说明**如何导出复杂内核符号**和
> **如何集成开源库**。

---

## 目录

1. [背景与架构](#1-背景与架构)
2. [前置依赖](#2-前置依赖)
3. [编译内核](#3-编译内核)
4. [编译测试模块与打包](#4-编译测试模块与打包)
5. [QEMU 运行验证](#5-qemu-运行验证)
6. [编写你自己的模块](#6-编写你自己的模块)
7. [导出内核符号（简单 + 复杂面）](#7-导出内核符号简单--复杂面)
8. [集成开源库（以 mbedTLS 为例）](#8-集成开源库以-mbedtls-为例)
9. [重定位类型 → 如何触发 速查表](#9-重定位类型--如何触发-速查表)
10. [故障排查](#10-故障排查)
11. [已知限制](#11-已知限制)

---

## 1. 背景与架构

dlmodule 是 RT-Thread 内建的一个轻量级动态加载器（`components/libc/posix/libdl/`），
等价于一个跑在内核特权级（EL1）的迷你 `ld.so`：模块与内核共享地址空间，通过
PLT/GOT 直接调用内核导出的函数。

| 文件 | 职责 |
|------|------|
| `dlmodule.c` | 模块生命周期、`dlmodule_symbol_find()` 内核符号查找、`dlmodule_load/exec` |
| `dlelf.c` | ELF 解析、PT_LOAD 拷贝、`.rela.*`/`.relr.dyn` 重定位、符号表构建 |
| `arch/aarch64.c` | AArch64 重定位类型处理（写地址 / 打指令） |
| `dlsyms.c` | 内核导出的 libc 符号（`strlen/memcpy/printf/...`） |
| `dlfcn.h` / `dlopen.c` / `dlsym.c` / `dlerror.c` | POSIX 风格 API |

**关键机制**：内核函数用 `RTM_EXPORT(symbol)` 导出后，被放入链接脚本的 `RTMSymTab`
段（`__rtmsymtab_start` ~ `__rtmsymtab_end`）。模块里任何**未定义符号**都会在这个表里
查找。模块自己则导出 `.dynsym` 里 `STT_FUNC`/`STT_OBJECT` 的全局符号，供 `dlsym()` 查找。

> 模块约定：导出名为 `module_init` 的函数会在加载后自动执行，`module_cleanup` 在卸载时执行。

---

## 2. 前置依赖

| 依赖 | 用途 | 版本/路径 |
|------|------|-----------|
| `aarch64-none-elf-gcc` | 编译内核 + 编译 `.so` | Arm GNU Toolchain 14.2（本仓库 `/opt/toolchains/.../bin`） |
| `qemu-system-aarch64` | 运行 | ≥ 8.2 |
| `scons` + `python3` | 编译内核 | 任意 |
| `mkfs.vfat` + `mcopy`（mtools） | 打包 FAT | `apt install mtools` |

工具链路径通过环境变量 `RTT_EXEC_PATH` 指定：

```bash
export RTT_EXEC_PATH=/opt/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin
```

---

## 3. 编译内核

```bash
cd bsp/qemu-virt64-aarch64
export RTT_EXEC_PATH=/opt/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin
scons -j4
```

产物：`rtthread.elf` / `rtthread.bin`。`rtconfig.h` 已开启 `RT_USING_MODULE`（dlmodule 开关）。

本仓库额外在 `applications/kernel_exports.c` 里导出了一组**复杂内核符号面**（见
[第 7 节](#7-导出内核符号简单--复杂面)），并在 `applications/test_cmd.c` 里注册了
`cmd_kapi` / `cmd_mbedtls` 等测试命令——都随 `scons` 一起编译，无需额外配置。

---

## 4. 编译测试模块与打包

`tests/dlmodule/build.sh` 一键从源码重建所有 `.so` 并打包 `modules.fat`：

```bash
cd tests/dlmodule
./build.sh
```

脚本做的事：

1. 用 `aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2` 编译所有单文件模块；
2. 编译 `mod_kapi.so`（消费复杂内核符号面）；
3. 若本地无 mbedTLS，`git clone` mbedTLS 2.28.9 到 `.deps/`，静态链成 `mod_mbedtls.so`；
4. `mkfs.vfat` + `mcopy` 把所有 `.so` 打进 `modules.fat` 根目录。

```bash
# 用已有的 mbedTLS 检出，避免重复 clone：
MBEDTLS_DIR=/path/to/mbedtls ./build.sh
```

---

## 5. QEMU 运行验证

```bash
cd tests/dlmodule
./build.sh          # 先构建
./verify_all.sh     # 再验证
```

`verify_all.sh` 会启动 QEMU（`-M virt -cpu max -smp 4`，与 BSP 默认 `qemu.py` 一致），
把 `modules.fat` 作为 `vda0` 挂载，然后向 msh 依次下发测试命令。期望看到：

```
mount device vda0(elm) onto / ... succeed!
dlsym('add') = 0x0x0000000040250220
call(10, 20) => 30
call(30, 3) => 90
[string_util_v1.0] module loaded
[sensor2] module loaded, 6 filters
call(500, 1) => 350
[dispatcher] module loaded, 10 handlers, 3 events
[allocator] module loaded, max_allocs=32
[mod_kapi] ALL CHECKS PASSED
=== kapi_run => 0 (PASS) ===
[mbedtls] SHA-256("abc") = ba7816bf8f01cfea... (ok)
[mbedtls] AES-128-CBC round-trip (32 bytes): 52542d54687265616420646c... (ok)
[mbedtls] ALL CHECKS PASSED
=== mbedtls_demo => 0 (PASS) ===
[stress] module loaded: 8 filters, 4 post-processors, ring=1024, pool=64
call(0) => 0
```

### 手动交互运行

```bash
cd bsp/qemu-virt64-aarch64
python qemu.py          # 或直接 qemu-system-aarch64（见 verify_all.sh 参数）
```

在 `msh />` 提示符下可用命令：

| 命令 | 作用 |
|------|------|
| `mount vda0 / elm` | 挂载 `modules.fat` 到根目录（**必做第一步**） |
| `cmd_load /mod_xxx.so` | 加载并列出导出符号 |
| `cmd_call /mod_xxx.so func [arg]` | `dlopen→dlsym→call(1 arg)` |
| `cmd_call2 /mod_xxx.so func a b` | 同上，2 个参数 |
| `cmd_kapi` | 复杂内核符号面端到端测试 |
| `cmd_mbedtls` | mbedTLS SHA-256 + AES 往返测试 |
| `cmd_cjson_demo` | cJSON 解析 demo |
| `cmd_dlerror /mod_xxx.so` | 加载并打印 `dlerror()` |
| `cmd_unload <name>` | 按名字卸载模块 |
| `list_module` / `list_device` | 查看已加载模块 / 块设备 |

---

## 6. 编写你自己的模块

最小模板（`hello.c`）：

```c
#ifndef NULL
#define NULL ((void*)0)
#endif

/* 1. 声明要用到的内核导出函数（原型要和内核一致） */
extern int rt_kprintf(const char *fmt, ...);

/* 2. 你的业务函数 —— 会被导出到 .dynsym，供 dlsym 调用 */
int add(int a, int b) { return a + b; }

/* 3. 加载后自动执行（loader 会 dlsym("module_init")） */
int module_init(void)
{
    rt_kprintf("[hello] loaded, add(1,2)=%d\n", add(1, 2));
    return 0;
}

void module_cleanup(void)
{
    rt_kprintf("[hello] unloaded\n");
}
```

编译（**注意必须 `-shared -fPIC -nostdlib`**）：

```bash
aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2 -o hello.so hello.c
```

放进 `modules.fat`，在 msh 里：

```
cmd_call2 /hello.so add 10 20     # => 30
```

### 模块编写规则（务必遵守）

1. **不链接任何 C 运行库**：`-nostdlib`，所有 `memcpy/memset/strlen/printf` 都走内核导出（`dlsyms.c`）。
2. **不依赖 `DT_NEEDED`**：loader 不会自动加载依赖库，多文件依赖要**静态链进同一个 `.so`**（见第 8 节）。
3. **别用 TLS / 浮点**：`__thread` 和 FP/SIMD 上下文目前不支持（见第 11 节）。
4. **别依赖 `main`**：入口是 `module_init`（无参返回 int）；`module_cleanup` 为可选的卸载回调。
5. **`dlsym` 能拿到函数和数据**：`STT_FUNC` 与 `STT_OBJECT` 的全局符号都会被导出。
6. **构造函数**：`__attribute__((constructor))` 会作为 `.init_array` 在重定位后被 loader 调用（已支持）。

### 模块与内核的编译参数对照

**模块编译参数不需要和内核完全一致**，但分三类：一类必须一致，一类必须故意不同，一类无所谓。

**① 必须一致 —— ABI 硬约束（不一致会错位/崩溃）**

| 参数 | 内核（`rtconfig.py`） | 模块（`build.sh`） | 说明 |
|------|----------------------|-------------------|------|
| `-march` 指令集基线 | `armv8-a` | `armv8-a` | 模块指令集**不能高于**内核/CPU 支持；用同款基线最安全 |
| `-mabi=lp64` | lp64 | lp64 | 64 位数据模型（`long`/指针 8 字节），绝不能一边 ilp32 |
| `char` 符号性 | 默认 **unsigned** | 默认 **unsigned** | AArch64 上 `char` 默认无符号（`__CHAR_UNSIGNED__=1`），**与 x86 相反**；跨边界传 `char` 尤其要两边一致 |
| 对齐 / 打包 | 默认非 `-mstrict-align` | 默认非 `-mstrict-align` | 两边都不开 `-fshort-enums`/`-fpack-struct`，若内核开了模块必须同开 |

**② 必须故意不同 —— 动态加载成立的前提**

| 参数 | 内核 | 模块 | 为什么 |
|------|------|------|--------|
| 链接方式 | `-static`（绝对地址 + `link.lds`） | `-shared -fPIC` | 模块要能搬到任意地址，靠 `RELATIVE/GLOB_DAT/JUMP_SLOT` 绑定到内核 |
| 运行库 | 链接 libgcc/libsupc++ | `-nostdlib` | 模块的 `memcpy/memset/strlen` 等全走内核 `dlsyms.c` 导出，不自带实现 |
| 链接脚本 | `link.lds`（含 `RTMSymTab`） | gcc 默认 dynamic 段 | 模块不套内核链接脚本 |

**③ 无所谓（建议保持一致即可）**

| 参数 | 说明 |
|------|------|
| 优化级别 `-O0` / `-O2` | 不影响 ABI。测试里内核 `-O0`、模块 `-O2`。`-O0` 反而更容易产出 `ADRP+ADD`/`MOVW_G` 这类指令修补型重定位，测得更全 |
| 调试信息 `-g`、告警 `-Wall` | 无影响 |
| `-ffast-math` / `-ftree-vectorize` | 只改浮点语义、可能引入 NEON——模块已有 FP 上下文限制，**建议模块别开、别用浮点** |

> **关键真相**：编译参数一致**不是**正确性的充分条件。跨内核边界真正生效的是「`extern` 声明与内核实现逐字节一致」——签名、结构体布局、枚举值、`char` 符号性，这些都靠**头文件一致**保证，而非靠编译命令对齐。所以最稳妥的做法是：内核导出什么，模块就 `#include` 同一份头文件（或精确镜像的 `extern` + struct）。

> **工具链兼容性**：实测 `aarch64-none-elf-gcc` 与 `aarch64-linux-gnu-gcc` 在 `char` 符号性、`-mabi`、`-mstrict-align`、`x18` 平台寄存器默认行为上均一致，所以同架构下用不同工具链编内核/模块，ABI 层面是兼容的。

---

## 7. 导出内核符号（简单 + 复杂面）

### 7.1 简单导出

在内核任意 `.c` 文件里 `#include <rtm.h>`，然后：

```c
RTM_EXPORT(my_func);      /* 函数 */
RTM_EXPORT(my_global);    /* 数据 */
```

模块里 `extern` 声明后即可调用/访问，loader 加载时会用 `dlmodule_symbol_find()` 解析。

### 7.2 复杂导出面（推荐参照 `applications/kernel_exports.c`）

真实驱动/服务往往不止导出函数，还会暴露**可写数据、常量表、函数指针结构体、IPC 对象、
回调注册**。这些跨边界访问模式对应的重定位类型不同，验证时需要**都覆盖**：

| 模式 | 内核侧 | 模块侧触发 | 重定位 |
|------|--------|-----------|--------|
| 可写全局数据 | `int kapi_state = 0xA5A5...;` | 读/写 `kapi_state` | `GLOB_DAT` |
| 只读常量表 | `const kapi_ent_t kapi_lut[8];` | 读 `kapi_lut[i].gain` | `GLOB_DAT` |
| 函数指针结构体 | `kapi_ops_t kapi_ops = { .read=..., ... };` | `ops->read(...)` 间接调用 | `GLOB_DAT` |
| 富签名函数 | `int kapi_transform(...)` | 直接调用 | `JUMP_SLOT` |
| IPC 访问器 | `rt_sem_create/take/release` 包装 | 调用 `kapi_ipc_wait/signal` | `JUMP_SLOT` |
| 回调注册 | `void kapi_set_event_cb(cb)` | 传函数指针进内核，内核回叫 | 双向函数指针 |

对应模块见 `mod_kapi.c`，一条 `cmd_kapi` 跑完全部检查。**新增内核导出时**：

1. 在 `applications/kernel_exports.c`（或你的驱动）里写实现 + `RTM_EXPORT(...)`；
2. 在测试模块里 `extern` 声明并用同样的 ABI 结构体访问；
3. `scons` 重建内核（确认 `rtthread.map` 里 `RTMSymTab` 段出现新符号）；
4. 重新 `build.sh` + QEMU 验证。

> 校验技巧：`grep my_func rtthread.map` 应同时看到实现地址和 `__rtmsym_my_func` 表项。

---

## 8. 集成开源库（以 mbedTLS 为例）

mbedTLS 是一个多文件、纯 C、无 TLS/FP 的嵌入式加密库，是「开源库 + 内核符号导出」
结合的典型样例。要点：

1. **静态链成单个自包含 `.so`**（`DT_NEEDED` 不支持，所以不能是「库 .so + 依赖 .so」）：
   ```bash
   aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2 \
     -DMBEDTLS_CONFIG_FILE='"mbedtls_config_min.h"' \
     -I tests/dlmodule -I mbedtls/include \
     -o mod_mbedtls.so \
     mbedtls/library/aes.c mbedtls/library/sha256.c \
     mbedtls/library/platform_util.c mbedtls/library/platform.c \
     mod_mbedtls_demo.c
   ```

2. **裁剪 + 重定向堆**（`mbedtls_config_min.h`）：
   ```c
   #define MBEDTLS_AES_C
   #define MBEDTLS_CIPHER_MODE_CBC
   #define MBEDTLS_SHA256_C
   #define MBEDTLS_PLATFORM_C
   #define MBEDTLS_PLATFORM_MEMORY
   #define MBEDTLS_PLATFORM_CALLOC_MACRO   rt_calloc   /* 库堆 → 内核 rt_calloc */
   #define MBEDTLS_PLATFORM_FREE_MACRO     rt_free
   #include "mbedtls/check_config.h"
   ```
   这样库的 `mbedtls_calloc/free` 直接变成对内核导出 `rt_calloc/rt_free` 的 `JUMP_SLOT` 调用，
   库内部再无 libc 堆依赖；`memcpy/memset` 等由内核 `dlsyms.c` 解析。

3. **demo 做真实验证**：SHA-256 对 NIST 已知向量比对 + AES-128-CBC 加解密往返，
   一次性触发大量 `JUMP_SLOT` + `GLOB_DAT` + `RELATIVE`。

`build.sh` 已把上述流程自动化（自动 clone mbedTLS 2.28.9）。

---

## 9. 重定位类型 → 如何触发 速查表

dlmodule 在 AArch64 `.so`（ET_DYN）路径真正会用到的动态重定位类型及触发方式：

| 类型 | 值 | 含义 | 在 `.so` 里如何触发 |
|------|----|------|--------------------|
| `R_AARCH64_RELATIVE` | 1027 | `*where = base + addend` | 全局函数指针/数据指针的静态初始化（函数指针表） |
| `R_AARCH64_GLOB_DAT` | 1025 | `*where = S + A` | `extern` 一个内核数据符号 / 常量表 |
| `R_AARCH64_JUMP_SLOT` | 1026 | `*where = S + A`（经 PLT） | 调用内核导出函数（`rt_kprintf` 等） |
| `R_AARCH64_ABS64` | 257 | `*where = S + A` | 绝对地址常量（少见，`-fno-pic` 的静态初始化） |
| `R_AARCH64_COPY` | 1024 | 从内核拷贝数据到模块 | 模块 `extern` 一个内核**有初值**的全局变量 |
| `R_AARCH64_IRELATIVE` | 1032 | 调 resolver 得函数地址 | IFUNC（裸机 gcc 不产此类型，loader 已按两遍处理） |
| `R_AARCH64_NONE` | 0 | 空操作 | 恒存在 |

> 指令修补型（`CALL26/JUMP26/ADR_PREL_PG_HI21/ADD_ABS_LO12_NC/LDST*/MOVW_G*`）只在
> **ET_REL（`.o`）**里出现；`.so` 的静态链接阶段已把它们消解，加载器对 ET_REL 仅有限支持（见第 11 节）。

---

## 10. 故障排查

### 10.1 加载失败查 `dlerror`

```c
// 内核里 / 模块里：
const char *err = dlerror();
```

msh 里直接：`cmd_dlerror /mod_xxx.so`。常见的 `dlerror` 输出：

| 报错 | 原因 | 处理 |
|------|------|------|
| `can't find X in kernel symbol table` | 模块引用了未导出的符号 | 内核里给 `X` 加 `RTM_EXPORT(X)` 并 `scons` |
| `ELF machine error (got N, expected 183)` | 用错了架构的 ELF | 用 `aarch64-none-elf-gcc` 重编 |
| `allocate space failed` | 内存不足 / 对齐失败 | 检查 `rt_malloc_align` 可用内存 |

### 10.2 模块加载后崩溃

- **跳空 / EPC 是垃圾值**：某个指针没被重定位。检查是否用了 `.relr.dyn`（RELR 已支持）或未解析符号。
- **数据错位**：`p_vaddr != 0` 的 ET_DYN 需保证 `mem_space` 满足 `p_align`；loader 已用 `rt_malloc_align` 对齐。
- **浮点/SIMD**：模块里用了浮点而内核没保存 FP 上下文，会崩（见 11）。

### 10.3 dlsym 找不到符号

- 模块内函数/变量必须是**全局**（非 `static`），且不带 `-Wl,--gc-sections` 把未引用段裁掉。
- 数据符号（`STT_OBJECT`）已支持 dlsym，确认类型不是 `static`。

### 10.4 挂载失败

`mount vda0 / elm` 报错：确认 `modules.fat` 是 `mkfs.vfat` 生成的 FAT 且已作为块设备挂上（`list_device` 看 `vda0`）。

---

## 11. 已知限制

1. **TLS**：`__thread` 不支持（裸机工具链走 emutls，内核无 `__emutls_get_address` 运行时）。已列为边界，后续若要支持优先做 emutls。
2. **FP/SIMD 上下文**：内核需在调用模块前后保存/恢复 FP 寄存器，否则浮点模块会崩。
3. **`DT_NEEDED`**：不自动加载依赖，必须静态链成单个 `.so`。
4. **ET_REL（`.o`）**：仅有限支持——指令修补型重定位（`CALL26/ADR_*` 等）只在 `.o` 里出现，加载器对 ET_REL 的符号表与地址计算有已知缺陷（详见 `BUGS.md` #2/#8），**生产请用 `.so`**。
5. **IFUNC**：`aarch64-none-elf-gcc` 不支持 `ifunc` 属性，无法从 C 源码自然生成 `IRELATIVE`；loader 侧已按「先普通重定位、后 IRELATIVE」两遍正确处理。

---

## 附：本目录文件清单

| 文件 | 说明 |
|------|------|
| `GUIDE.md` | 本文档 |
| `build.sh` | 重建所有 `.so` + 打包 `modules.fat` |
| `verify_all.sh` | QEMU 端到端验证（相对路径，可直接运行） |
| `README.md` | 实现与重定位类型参考 |
| `BUGS.md` | 已知 bug 清单与修复记录 |
| `mbedtls_config_min.h` | mbedTLS 最小裁剪 + 堆重定向配置 |
| `mod_kapi.c` | 复杂内核符号面测试模块 |
| `mod_mbedtls_demo.c` | mbedTLS 集成 demo |
| `mod_{minimal,ext1,ext2,string_util,sensor2,dispatcher,allocator,reloc_full,stress}.c` | 基础/压力测试模块 |
| `kernel_exports.c`（在 `bsp/.../applications/`） | 复杂内核符号导出面 |
| `test_cmd.c`（在 `bsp/.../applications/`） | msh 测试命令 |
