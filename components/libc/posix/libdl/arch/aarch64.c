/*
 * Copyright (c) 2006-2024 RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2024/06/26     RT-Thread Team    first version
 */

#include "../dlmodule.h"
#include "../dlelf.h"

#ifdef __aarch64__

#define R_AARCH64_NONE              0    /* No relocation */
#define R_AARCH64_ABS64           257    /* Direct 64 bit: S + A */
#define R_AARCH64_GLOB_DAT       1025    /* Create GOT entry: S + A */
#define R_AARCH64_JUMP_SLOT      1026    /* Create PLT entry: S + A */
#define R_AARCH64_RELATIVE       1027    /* Adjust by program base: Delta(S) + A */

#define DBG_TAG           "posix.libdl.arch"
#define DBG_LVL           DBG_INFO
#include <rtdbg.h>

int dlmodule_relocate(struct rt_dlmodule *module, Elf_Rel *rel, Elf_Addr sym_val)
{
    Elf64_Addr *where;

    where = (Elf64_Addr *)((rt_uint8_t *)module->mem_space
                           + rel->r_offset
                           - module->vstart_addr);

    switch (ELF64_R_TYPE(rel->r_info))
    {
        case R_AARCH64_NONE:
            break;
        case R_AARCH64_ABS64:
            *where = (Elf64_Addr)(sym_val + rel->r_addend);
            LOG_D("R_AARCH64_ABS64: %p -> 0x%lx", where, *where);
            break;
        case R_AARCH64_GLOB_DAT:
            *where = (Elf64_Addr)(sym_val + rel->r_addend);
            LOG_D("R_AARCH64_GLOB_DAT: %p -> 0x%lx", where, *where);
            break;
        case R_AARCH64_JUMP_SLOT:
            *where = (Elf64_Addr)(sym_val + rel->r_addend);
            LOG_D("R_AARCH64_JUMP_SLOT: %p -> 0x%lx", where, *where);
            break;
        case R_AARCH64_RELATIVE:
            *where = (Elf64_Addr)((rt_uint8_t *)module->mem_space
                                  - module->vstart_addr
                                  + rel->r_addend);
            LOG_D("R_AARCH64_RELATIVE: %p -> 0x%lx", where, *where);
            break;
        default:
            LOG_D("AARCH64ELF: invalid relocate TYPE %d", ELF64_R_TYPE(rel->r_info));
            return -1;
    }

    return 0;
}

#endif /* __aarch64__ */
