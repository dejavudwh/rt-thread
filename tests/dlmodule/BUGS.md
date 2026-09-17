# AArch64 dlmodule 分支 — Bug 清单

> 复现环境：QEMU 8.2.2 + `aarch64-none-elf-gcc`（Arm GNU Toolchain 14.2），BSP `bsp/qemu-virt64-aarch64`
> 复现日期：2026-09-17
> 复现方法：自建含 `.so` 的 FAT 镜像挂 `vda0`，QEMU 启动到 `msh />`，用 `cmd_load` / `cmd_call` / `cmd_call2` 实测
> 分支：`feature/aarch64-dlmodule`（相对 `master` 领先 10 个提交）

---

## Bug 一览（按严重度排序）

| # | 标题 | 严重度 | 触发条件 | 状态 |
|---|------|--------|----------|------|
| 1 | RELR 压缩重定位缺失 → 内核崩溃 | 🔴 致命 | 模块用 `-z pack-relative-relocs` 打包 | ✅ 已修复 |
| 2 | ET_REL 模块加载后符号表为空 | 🔴 高 | 加载 `.o`（ET_REL）模块 | ⏸️ 标记为有限支持 |
| 3 | TLS 模块无法加载 | 🟠 中 | 模块含 `__thread` 变量 | ⏸️ 已知限制（见 TLS 必要性分析） |
| 4 | DT_INIT_ARRAY 不解析，constructor 静默不执行 | 🟠 中 | 模块含 `__attribute__((constructor))` | ✅ 已修复 |
| 5 | `e_machine` 从不校验 | 🟠 中 | 加载错误架构的 ELF | ✅ 已修复 |
| 6 | `verify_all.sh` 硬编码路径错误 | 🟡 低 | 运行验证脚本 | ⏸️ 未修（待定） |
| 7 | dlsym 找不到数据符号（STT_OBJECT） | 🟠 中 | dlsym 一个全局变量 | ✅ 已修复 |
| 8 | ET_REL 重定位地址计算错误 | 🔴 高 | ET_REL 模块非 `.text` 段重定位 | ⏸️ 标记为有限支持 |
| 9 | `dlerror()` 是 stub，永远返回 "TODO" | 🟡 低 | 加载失败时查错误原因 | ✅ 已修复 |

---

## 详情

### Bug 1 — RELR 压缩重定位缺失（致命）

**现象**：编译一个含函数指针表、用 `-z pack-relative-relocs` 打包的 `.so`（`.relr.dyn` 段含 2 个 RELATIVE 重定位），加载后调用：

```
msh />cmd_call2 /mod_relr_test.so relr_dispatch 0 10 20
[E/libcpu.trap] ... EPC :0x0000000000000248   ← 跳到未重定位的垃圾地址，内核 trap
```

**根因**：`dlelf.c` 的 `dlmodule_load_shared_object` 只遍历 `SHT_RELA` 段（`IS_RELA`），完全忽略 `SHT_RELR`（`.relr.dyn`）。用 RELR 打包的模块，其 RELATIVE 重定位全部漏掉，函数指针表保持原始偏移值（接近 0），调用即跳空。

**建议修复**：解析 `.relr.dyn` 段（或 PT_DYNAMIC 里的 `DT_RELR`/`DT_RELRSZ` tag），实现 RELR 位图解码——8 字节一组，低 63 位为地址，最高位为 1 表示后跟一个非压缩的绝对地址项。

**复现材料**：`mod_relr_test.c`，编译 `-Wl,-z,pack-relative-relocs`。

---

### Bug 2 — ET_REL 模块符号表为空（高）

**现象**：

```
msh />cmd_load /mod_complex.o
OK: 6057 bytes, 0 symbols:      ← 0 个导出符号
```

`dlsym` 找不到 `complex_run` 等任何函数。

**根因**：`dlmodule_load_relocated_object`（`dlelf.c`）只解析 `.dynsym` 段，而 ET_REL（`.o`）只有 `.symtab`（`readelf -S mod_complex.o` 确认只有 `.symtab`）。符号表永远为空。

**影响**：`mod_complex.o` 声称"触发全部 23 类指令修补型重定位"（实测含 22 ADR_PRE + 22 ADD_ABS + 21 ABS64 + 7 CALL26 + 6 ADR_GOT + 6 LD64_GO），但**这些重定位类型从没在 QEMU 里真正跑通过**，只存在于代码层面。

**建议修复**：ET_REL 分支改用 `.symtab` 构建 `module->symtab`（对 STT_FUNC/STT_OBJECT 的 GLOBAL 符号）。

**复现材料**：`tests/dlmodule/mod_complex.o`。

---

### Bug 3 — TLS 模块无法加载（中）

**现象**：

```
msh />cmd_load /mod_tls_test.so
[E/DLMD] Module: can't find __emutls_get_address in kernel symbol table
FAILED
```

**根因**：裸机工具链（`aarch64-none-elf`）的 `__thread` 变量走 **emutls**（软件 TLS），反汇编确认 `tls_bump` 调用 `__emutls_get_address@plt`。内核符号表无该运行时函数，且 loader 无任何 TLS 重定位处理。

**建议**：列为已知限制（README 已声明）。详见下方「TLS 必要性分析」。若后续要支持，优先实现 emutls 运行时（`__emutls_get_address` 等），而非硬件 TLS。

---

### Bug 4 — DT_INIT_ARRAY 不解析（中）

**现象**：

```
msh />cmd_call /mod_ctor_test.so check_ctor 0
[ctor] module_init called, ctor_ran = 0    ← 应为 42
call(0) => 0
```

**根因**：`.init_array` 里有 `my_ctor`，动态段有 `INIT_ARRAY`(0x10390)/`INIT_ARRAYSZ`(8) tag，但 loader 只靠 `dlsym(module, "module_init")` 名字约定调用入口，从不解析 DT_INIT_ARRAY。

**影响**：任何依赖 `__attribute__((constructor))` 做初始化的 `.so` 都会静默错（`ctor_ran` 恒为 0）。

**建议修复**：加载后遍历 `.init_array`（或 DT_INIT_ARRAY），按顺序调用每个构造器函数指针。

---

### Bug 5 — e_machine 从不校验（中）

**现象**：把 `mod_minimal.so` 的 machine 字段（ELF offset 18）改成 `EM_X86_64`(62) 后：

```
msh />cmd_call2 /mod_wrong_machine.so add 10 20
call(10, 20) => 30      ← 照单全收，无任何报错
```

**根因**：`dlmodule_load`（`dlmodule.c`）只校验 magic（`\177ELF`/`\177RTM`）和 ELF class（32/64），从不检查 `e_machine`。错误架构的文件被当作 AArch64 指令 patch，静默损坏内存。

**建议修复**：在 `dlmodule_load` 增加 `e_machine == EM_AARCH64`(183) 校验。

---

### Bug 6 — verify_all.sh 硬编码路径错误（低）

**现象**：真实运行脚本：

```
qemu-system-aarch64: -drive file=/home/ubuntu/cluade-workspaces/rt-thread-aarch64/tests/dlmodule/modules.fat ...
  Could not open ...: No such file or directory
```

**根因**：`verify_all.sh:11-12` 硬编码：
```bash
BSP=/home/ubuntu/cluade-workspaces/rt-thread-aarch64/bsp/qemu-virt64-aarch64   # "cluade" 拼写错误
IMG=/home/ubuntu/cluade-workspaces/rt-thread-aarch64/tests/dlmodule/modules.fat
```
路径双重错误：拼写 `cluade`（应为 claude）+ 指向 6 月 29 日的旧仓库副本（无 `tests/dlmodule`）。

**建议修复**：改为相对路径（`$(dirname "$0")/..`）或当前仓库路径。

---

### Bug 7 — dlsym 找不到数据符号（STT_OBJECT）（中）

**现象**：加载 `mod_got_test.so`（含全局变量 `int g_counter`）后：

```
msh />cmd_load /mod_got_test.so
OK: 66632 bytes, 4 symbols:
  [0] got_write_read  [1] got_sum  [2] module_cleanup  [3] module_init

msh />cmd_call /mod_got_test.so g_counter 0
dlsym('g_counter') NOT FOUND.
```

**根因**：`dlelf.c` 构建 `module->symtab` 时只收 `ELF_ST_BIND==STB_GLOBAL && ELF_ST_TYPE==STT_FUNC` 的符号，`STT_OBJECT`（全局变量）被静默丢弃。POSIX `dlsym` 应能同时解析函数和数据符号，这里是语义缺口。

**影响**：无法通过 `dlsym` 读取模块导出的全局变量；模块间共享数据只能靠 `GLOB_DAT`/`COPY` 间接完成。

**建议修复**：构建 symtab 时把 `STT_OBJECT` 也纳入（或至少 `STT_OBJECT | STT_FUNC` 的 GLOBAL 符号）。

---

### Bug 8 — ET_REL 重定位地址计算错误（高）

**现象**：与 Bug 2 同源但独立。`mod_complex.o`（ET_REL）加载后「0 symbols」，即使补上 Bug 2 的符号表修复，调用其函数仍会因重定位地址算错而崩溃。

**根因**：`dlmodule_load_relocated_object`（`dlelf.c`）把 text/rodata/data/bss **连续拼接**进一个 flat buffer（`ptr += sh_size`），但 `aarch64.c` 的 `dlmodule_relocate` 计算 `where` 时用：
```c
where = mem_space + rel->r_offset - vstart_addr;   // ET_REL 时 vstart_addr=0
```
`r_offset` 对 ET_REL 是**节内偏移**，不是 flat buffer 偏移。所以：
- `.rela.text` 里的重定位（`.text` 恰好是第一个加载段，flat 偏移 0）→ `where` 碰巧正确；
- `.rela.data*`、`.rela.rodata` 等非首段的重定位 → `where` 指向 `.text` 区域，patch 到错误地址。

实测 `mod_complex.o` 的节序：`.text`(sh_addr=0) → `.data`(0) → `.bss` → `.data.rel.local` → `.rodata`，且存在**两个 data 节**（`.data` + `.data.rel.local`），`data_addr` 会被后一个覆盖。

**影响**：ET_REL（`.o`）加载路径是**双重损坏**的（符号表空 + 地址算错），所谓「23 类指令修补型重定位」从未在真实路径工作过。

**建议修复**：ET_REL 分支需按节的实际 flat 偏移计算 `where`（记录每个节的 `sh_addr → flat offset` 映射），而非直接用 `r_offset`；符号地址同理。

---

### Bug 9 — `dlerror()` 是 stub（低）

**现象/根因**：`dlerror.c` 全文：
```c
const char *dlerror(void) { return "TODO"; }
```
任何加载失败（如 Bug 1/3/5 的失败路径）都无法拿到错误详情，只有 `[E/DLMD]` 日志。README 声称的「完整实现」缺了 POSIX 的 `dlerror`。

---

## 代码级问题（非运行时，需 code review 确认）

以下已从代码确认，未逐一运行时验证：

| 位置 | 问题 |
|------|------|
| `aarch64.c:240-247` COPY | 固定拷贝 8 字节（`sizeof(Elf64_Addr)`），>8 字节的 copy 符号被截断；根因是 `dlmodule_relocate` 签名拿不到 `st_size` |
| `aarch64.c:252-258` IRELATIVE | 按 section 顺序直接调 resolver，无「IRELATIVE 最后处理」排序，用 IFUNC 的 `.so` 可能崩 |
| `aarch64.c:379-387` MOVW 非 `_NC` 溢出检查 | 空 `if` 块（注释写 "ZERO check" 但函数体为空），README 第 99 行「validated strictly」与代码不符 |
| `dlelf.h:355` `IS_ALLOC` | `sh_flags == SHF_ALLOC`（`==` 而非 `&`），带 `SHF_WRITE`/`SHF_EXECINSTR` 的段会判假；当前靠 IS_AX/IS_AW 覆盖「歪打正着」 |
| `dlelf.c:163` `static rt_bool_t unsolved` | 函数内 static，跨模块/跨调用共享，语义脆弱；「一个未解析符号 → 整体失败」是 all-or-nothing |
| `dlelf.c:132` 对齐 | `rt_malloc_align(size + max_align, max_align)` 只保证 `mem_space` 自身对齐，未保证 `p_vaddr mod p_align == load_addr mod p_align`（`p_vaddr != 0` 的 ET_DYN 会错位） |
| `test_cmd.c` cmd_load/cmd_call | 只 `dlmodule_load` 不 `dlclose`，每次调用泄漏一个模块 + 其符号表内存 |

---

## 修复记录（2026-09-17）

修复了 5 个 ET_DYN 相关 bug（Bug 1/4/5/7/9），全部在 QEMU 实测验证通过：

| Bug | 修复文件 | 验证结果 |
|-----|----------|----------|
| 1 RELR | `dlelf.c` 加 `.relr.dyn` 位图解码 | `relr_add(10,20)=>30`、`relr_mul(6,7)=>42`（原崩溃 EPC=0x248） |
| 4 DT_INIT_ARRAY | `dlelf.c` 加 `.init_array` 调用 | `ctor_ran=42`（原恒为 0） |
| 5 e_machine | `dlelf.h` 加 EM_* + `dlmodule.c` 校验 | `ELF machine error (got 62, expected 183)`（原照单全收） |
| 7 dlsym 数据符号 | `dlelf.c` symtab 纳入 STT_OBJECT | `g_counter` 可 dlsym（原 NOT FOUND） |
| 9 dlerror | `dlerror.c` 重写 + `dlmodule_seterror/clearerror` | 返回真实错误 / 成功清空 |

回归测试：7 个原始 `.so` 模块 + GOT 全局变量路径全部正常，无回归。

ET_REL（Bug 2/8）按用户决定**未修**，改为在 `aarch64.c` 顶部注释标注「[ET_REL] 指令修补型重定位仅 ET_REL 可达，loader 对 ET_REL 是有限支持」，并链接到本文件。

## 附带发现（非 bug，但需注意）

- 原始 `modules.fat` 里**缺 `mod_complex.o`**（提交 8693325 声称加入，但镜像里没有）。
- README 声称 8 个测试模块 + 贴了 `mod_reloc_full`/`mod_stress` 的 QEMU 输出，但 `verify_all.sh` 只测 7 个 `.so` 且不含它们。实测这两个模块本身能正常加载运行（是"脚本漏测"而非"代码跑不通"）。

---

## 已排除的误判（避免后人重复踩坑）

### 误判 1：「fat 装源码」
**结论：错误。** 用 QEMU 实际挂载原始 `modules.fat`，里面装的是 **11 个正确的 `.so`**（外加一个 `mod_cjson.so`），不是 `.c` 源码。之前 grep 到的 `mod_*.c` 是 `.so` 文件**内容里嵌入的源文件名字符串**（这些 `.so` 是 `not stripped`，带 `.symtab`）。

### 误判 2：「GOT 寻址重定位（ADR_GOT_PAGE/LD64_GOT_LO12_NC）实现错误」
**结论：需修正。** 实测发现 ET_DYN（`.so`）里模块内全局变量访问走 **GLOB_DAT** 路径（GOT 表项 + GLOB_DAT 填充），反汇编确认无 `ADR_GOT_PAGE`/`LD64_GOT_LO12_NC` 动态重定位。所以 `aarch64.c` 里这两个类型的实现是**死代码**（ET_DYN 触发不到，仅 ET_REL 出现），其语义实现虽可疑但实际不可达。实测 `mod_got_test.so` 的 GLOB_DAT 全局变量读写完全正确。

---

## TLS 必要性分析（结论）

**现阶段不必支持 TLS，建议列为已知限制，优先修 Bug 1 / 4 / 5。** 理由：

1. **使用频率极低**：dlmodule 的目标是嵌入式功能模块（驱动、算法、应用逻辑），这类代码几乎不用 `__thread`。用 TLS 的通常是通用库（日志、加密、locale、C++ 运行时），而这些库在 dlmodule 场景本就少见（还依赖更多 libc 符号）。

2. **实现成本高**：裸机工具链走 emutls（软件 TLS），要支持得实现 `__emutls_get_address` 运行时，且要处理"动态加载模块的 TLS 块与线程生命周期交互"（线程创建/切换/销毁时 TLS 块的注册与清理），这个 glibc 的 dlopen 也没完全做干净。

3. **上游现状**：RT-Thread 上游 dlmodule 的 arm/riscv/x86 arch 文件都无 TLS 处理，TLS 支持是超出上游现状的增强，不是缺口。

4. **有更紧急的 bug**：RELR 缺失会崩内核（真实用户用 `-z pack-relative-relocs` 就踩）、DT_INIT_ARRAY 是标准初始化机制、e_machine 是健壮性，都排在 TLS 前面。

**边界**：若目标升级为"运行任意第三方库"（尤其用了 TLS 的），TLS 会变硬需求，届时优先做 emutls 运行时（改动小、够用），而非硬件 TLS（需初始化 TPIDR_EL0 + 处理 TLSDESC，改动大）。
