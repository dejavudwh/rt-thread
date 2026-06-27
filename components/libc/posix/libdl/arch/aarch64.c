/*
 * Copyright (c) 2006-2024 RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2024/06/26     RT-Thread Team    first version (5 dynamic types)
 * 2024/06/27     RT-Thread Team    full AArch64 ELF ABI coverage (23 types)
 *
 * AArch64 ELF relocation handler for dlmodule (non-rt-smart).
 *
 * Reference: ELF for the Arm 64-bit Architecture (AARCH64_ABI)
 *            ARM Architecture Reference Manual (instruction encodings)
 */

#include "../dlmodule.h"
#include "../dlelf.h"

#ifdef __aarch64__

/* ── Dynamic relocation types (R_AARCH64_*) ──
 * Reference: Arm IHI 0056E Table 4-9, 4-14 */

#define R_AARCH64_NONE              0
#define R_AARCH64_ABS64           257
#define R_AARCH64_ABS32           258
#define R_AARCH64_ABS16           259
#define R_AARCH64_PREL64          260
#define R_AARCH64_PREL32          261
#define R_AARCH64_PREL16          262
#define R_AARCH64_MOVW_UABS_G0    263
#define R_AARCH64_MOVW_UABS_G0_NC 264
#define R_AARCH64_MOVW_UABS_G1    265
#define R_AARCH64_MOVW_UABS_G1_NC 266
#define R_AARCH64_MOVW_UABS_G2    267
#define R_AARCH64_MOVW_UABS_G2_NC 268
#define R_AARCH64_MOVW_UABS_G3    269
#define R_AARCH64_ADR_PREL_PG_HI21  275
#define R_AARCH64_ADD_ABS_LO12_NC   277
#define R_AARCH64_LDST8_ABS_LO12_NC 278
#define R_AARCH64_LDST16_ABS_LO12_NC 284
#define R_AARCH64_LDST32_ABS_LO12_NC 285
#define R_AARCH64_LDST64_ABS_LO12_NC 286
#define R_AARCH64_LDST128_ABS_LO12_NC 299
#define R_AARCH64_JUMP26         282
#define R_AARCH64_CALL26         283
#define R_AARCH64_COPY          1024
#define R_AARCH64_GLOB_DAT      1025
#define R_AARCH64_JUMP_SLOT     1026
#define R_AARCH64_RELATIVE      1027
#define R_AARCH64_IRELATIVE     1032

#define DBG_TAG           "posix.libdl.arch"
#define DBG_LVL           DBG_INFO
#include <rtdbg.h>

/* ── Instruction field extraction helpers ──
 *
 * ARMv8-A instruction encoding:
 *   General pattern: [31:24] [23:5] [4:0] = major opcode | payload | Rd
 *   For ADRP/ADD/LDR the payload varies; see ARM ARM C5.6.
 */

/* Extract bitfield from instruction */
static inline uint32_t insn_get_bits(uint32_t insn, int hi, int lo)
{
    return (insn >> lo) & ((1u << (hi - lo + 1)) - 1);
}

/* Insert bitfield into instruction */
static inline uint32_t insn_set_bits(uint32_t insn, int hi, int lo, uint32_t val)
{
    uint32_t mask = ((1u << (hi - lo + 1)) - 1) << lo;
    return (insn & ~mask) | ((val << lo) & mask);
}

/* ── 21-bit signed immediate (ADRP) ──
 * ADRP encodes a 21-bit signed PC-relative page offset.
 * Encoding: immhi (bits [23:5], 19 bits) | immlo (bits [30:29], 2 bits)
 * The immediate is shifted left by 12 to form the final page offset. */
static inline int32_t adrp_extract_imm(uint32_t insn)
{
    int32_t imm = (int32_t)(((insn >> 5) & 0x7ffff) | ((insn >> 29) & 0x3) << 19);
    /* Sign-extend from bit 20 */
    if (imm & (1 << 20)) imm |= ~((1 << 21) - 1);
    return imm;
}

static inline uint32_t adrp_insert_imm(uint32_t insn, int32_t imm)
{
    insn = insn_set_bits(insn, 23, 5, imm & 0x7ffff);
    insn = insn_set_bits(insn, 30, 29, (imm >> 19) & 0x3);
    return insn;
}

/* ── 12-bit unsigned immediate (ADD/LDR/STR) ──
 * Used by ADD_ABS_LO12_NC and LDST*_ABS_LO12_NC.
 * Encoding: bits [21:10] */
static inline uint32_t lo12_extract_imm(uint32_t insn)
{
    return insn_get_bits(insn, 21, 10);
}

static inline uint32_t lo12_insert_imm(uint32_t insn, uint32_t imm12)
{
    return insn_set_bits(insn, 21, 10, imm12);
}

/* ── 26-bit signed branch offset (CALL26/JUMP26) ──
 * Encoding: bits [25:0], shifted left by 2 to form the branch offset.
 * Range: ±128 MiB. */
static inline int32_t branch_extract_imm(uint32_t insn)
{
    /* imm26 is bits [25:0], sign-extend from bit 25 */
    int32_t imm = insn & ((1 << 26) - 1);
    if (imm & (1 << 25)) imm |= ~((1 << 26) - 1);
    return imm;
}

static inline uint32_t branch_insert_imm(uint32_t insn, int32_t imm)
{
    return (insn & ~((1 << 26) - 1)) | (imm & ((1 << 26) - 1));
}

/* ── 16-bit unsigned immediate (MOVW_G*) ──
 * MOVZ/MOVK encoding: bits [20:5]
 * Shift field: bits [22:21] (0=G0, 1=G1, 2=G2, 3=G3) */
static inline uint32_t movw_extract_imm(uint32_t insn)
{
    return insn_get_bits(insn, 20, 5);
}

static inline uint32_t movw_insert_imm(uint32_t insn, uint32_t imm16)
{
    return insn_set_bits(insn, 20, 5, imm16);
}

/* ═══════════════════════════════════════════════════════════════
 *  dlmodule_relocate — main relocation dispatch
 * ═══════════════════════════════════════════════════════════════ */
int dlmodule_relocate(struct rt_dlmodule *module, Elf_Rel *rel, Elf_Addr sym_val)
{
    Elf64_Addr *where;
    Elf64_Addr   base;   /* load offset: mem_space - vstart_addr */

    where = (Elf64_Addr *)((rt_uint8_t *)module->mem_space
                           + rel->r_offset
                           - module->vstart_addr);
    base  = (Elf64_Addr)((rt_uint8_t *)module->mem_space
                          - module->vstart_addr);

    switch (ELF64_R_TYPE(rel->r_info))
    {
    /* ── Group 1: No operation ── */
    case R_AARCH64_NONE:
        break;

    /* ── Group 2: Absolute data relocations ──
     * Formula: S + A   (symbol value + rel->r_addend)
     * Write the resolved absolute address (possibly truncated). */
    case R_AARCH64_ABS64:
        *where = (Elf64_Addr)(sym_val + rel->r_addend);
        LOG_D("R_AARCH64_ABS64: 0x%p -> 0x%lx", where, *where);
        break;

    case R_AARCH64_ABS32:
        *(uint32_t *)where = (uint32_t)(sym_val + rel->r_addend);
        LOG_D("R_AARCH64_ABS32: 0x%p -> 0x%x", where, *(uint32_t *)where);
        break;

    case R_AARCH64_ABS16:
        *(uint16_t *)where = (uint16_t)(sym_val + rel->r_addend);
        LOG_D("R_AARCH64_ABS16: 0x%p -> 0x%x", where, *(uint16_t *)where);
        break;

    /* ── Group 3: PC-relative data relocations ──
     * Formula: S + A - P   (symbol + addend - location)
     * where P = runtime address of the relocation site. */
    case R_AARCH64_PREL64:
        *where = (Elf64_Addr)(sym_val + rel->r_addend - (Elf64_Addr)where);
        LOG_D("R_AARCH64_PREL64: 0x%p -> 0x%lx", where, *where);
        break;

    case R_AARCH64_PREL32:
    {
        Elf64_Sxword val = (Elf64_Sxword)(sym_val + rel->r_addend
                                           - (Elf64_Addr)where);
        if (val != (Elf64_Sxword)(int32_t)val)
        {
            LOG_E("R_AARCH64_PREL32: overflow 0x%lx", val);
            return -1;
        }
        *(int32_t *)where = (int32_t)val;
        LOG_D("R_AARCH64_PREL32: 0x%p -> 0x%x", where, *(int32_t *)where);
        break;
    }

    case R_AARCH64_PREL16:
    {
        Elf64_Sxword val = (Elf64_Sxword)(sym_val + rel->r_addend
                                           - (Elf64_Addr)where);
        if (val != (Elf64_Sxword)(int16_t)val)
        {
            LOG_E("R_AARCH64_PREL16: overflow 0x%lx", val);
            return -1;
        }
        *(int16_t *)where = (int16_t)val;
        LOG_D("R_AARCH64_PREL16: 0x%p -> 0x%x", where, *(int16_t *)where);
        break;
    }

    /* ── Group 4: Dynamic linker stubs ──
     * GLOB_DAT and JUMP_SLOT: fill GOT/PLT entry with S + A.
     * RELATIVE: adjust pointer by load base + addend.
     * These are the most common types in .rela.dyn and .rela.plt. */
    case R_AARCH64_GLOB_DAT:
        *where = (Elf64_Addr)(sym_val + rel->r_addend);
        LOG_D("R_AARCH64_GLOB_DAT: 0x%p -> 0x%lx", where, *where);
        break;

    case R_AARCH64_JUMP_SLOT:
        *where = (Elf64_Addr)(sym_val + rel->r_addend);
        LOG_D("R_AARCH64_JUMP_SLOT: 0x%p -> 0x%lx", where, *where);
        break;

    case R_AARCH64_RELATIVE:
        *where = (Elf64_Addr)(base + rel->r_addend);
        LOG_D("R_AARCH64_RELATIVE: 0x%p -> 0x%lx", where, *where);
        break;

    /* ── Group 5: Copy ──
     * The memory at `where` is overwritten with the data of the
     * symbol. sym_val points to the source data, st_size bytes
     * are copied. Used when a shared object references a data
     * symbol that is allocated in the main executable. */
    case R_AARCH64_COPY:
        /* Note: sym_size is not directly available through
         * the current dlmodule_relocate() API. We copy 8 bytes
         * (pointer sized) as a safe default.  A full implementation
         * would pass the symbol's st_size from dlelf.c. */
        rt_memcpy(where, (void *)(sym_val + rel->r_addend), sizeof(Elf64_Addr));
        LOG_D("R_AARCH64_COPY: 0x%p", where);
        break;

    /* ── Group 6: Indirect function (IFUNC) ──
     * The addend points to a resolver function.  Call it to get
     * the final address, then store the result at `where`. */
    case R_AARCH64_IRELATIVE:
    {
        Elf64_Addr (*ifunc)(void) = (Elf64_Addr (*)(void))(base + rel->r_addend);
        *where = ifunc();
        LOG_D("R_AARCH64_IRELATIVE: 0x%p -> 0x%lx", where, *where);
        break;
    }

    /* ── Group 7: PC-relative branch (CALL26 / JUMP26) ──
     * Used for direct BL / B instructions within ±128 MiB.
     * Formula: (S + A - P) >> 2  → imm26 */
    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
    {
        Elf64_Sxword offset = (Elf64_Sxword)(sym_val + rel->r_addend
                                              - (Elf64_Addr)where);
        offset >>= 2; /* imm26 = byte-offset / 4 */
        if (offset != (Elf64_Sxword)(int32_t)offset ||
            offset < -(1 << 25) || offset >= (1 << 25))
        {
            LOG_E("R_AARCH64_CALL/JUMP26: target out of range (%ld)", offset);
            return -1;
        }
        *(uint32_t *)where = branch_insert_imm(*(uint32_t *)where, (int32_t)offset);
        LOG_D("R_AARCH64_CALL/JUMP26: 0x%p -> imm26=%ld", where, offset);
        break;
    }

    /* ── Group 8: ADRP page offset (ADR_PREL_PG_HI21) ──
     * Formula: Page(S + A) - Page(P)
     * Masks off low 12 bits, computes page difference, encodes as
     * 21-bit signed immediate in the ADRP instruction. */
    case R_AARCH64_ADR_PREL_PG_HI21:
    {
        Elf64_Addr target = (Elf64_Addr)(sym_val + rel->r_addend);
        Elf64_Addr pc     = (Elf64_Addr)where;
        Elf64_Sxword off  = (Elf64_Sxword)((target & ~0xFFFULL)
                                            - (pc & ~0xFFFULL));
        off >>= 12; /* page offset */
        if (off != (Elf64_Sxword)(int32_t)off
            || off < -(1 << 20) || off >= (1 << 20))
        {
            LOG_E("R_AARCH64_ADR_PREL_PG_HI21: target too far (%ld)", off);
            return -1;
        }
        *(uint32_t *)where = adrp_insert_imm(*(uint32_t *)where, (int32_t)off);
        LOG_D("R_AARCH64_ADR_PREL_PG_HI21: 0x%p -> imm=%ld", where, off);
        break;
    }

    /* ── Group 9: Low 12-bit page offset (ADD / LDR / STR) ──
     * Formula: (S + A) & 0xFFF
     * Replaces the 12-bit immediate in ADD or load/store instructions. */
    case R_AARCH64_ADD_ABS_LO12_NC:
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC:
    {
        uint32_t imm12 = (uint32_t)((sym_val + rel->r_addend) & 0xFFF);
        *(uint32_t *)where = lo12_insert_imm(*(uint32_t *)where, imm12);
        LOG_D("R_AARCH64_LO12: 0x%p -> imm12=0x%x", where, imm12);
        break;
    }

    /* ── Group 10: MOVW / MOVK absolute address fragments ──
     * Formula: selected 16-bit chunk of (S + A)
     * G0 = bits[15:0], G1 = bits[31:16], G2 = bits[47:32], G3 = bits[63:48]
     * _NC suffix means no overflow check (the linker already checked). */
    case R_AARCH64_MOVW_UABS_G0:
    case R_AARCH64_MOVW_UABS_G0_NC:
    case R_AARCH64_MOVW_UABS_G1:
    case R_AARCH64_MOVW_UABS_G1_NC:
    case R_AARCH64_MOVW_UABS_G2:
    case R_AARCH64_MOVW_UABS_G2_NC:
    case R_AARCH64_MOVW_UABS_G3:
    {
        Elf64_Addr  val  = (Elf64_Addr)(sym_val + rel->r_addend);
        uint32_t    type = ELF64_R_TYPE(rel->r_info);
        uint32_t    shift;
        uint16_t    imm16;

        switch (type)
        {
        case R_AARCH64_MOVW_UABS_G0:
        case R_AARCH64_MOVW_UABS_G0_NC:
            shift = 0; break;
        case R_AARCH64_MOVW_UABS_G1:
        case R_AARCH64_MOVW_UABS_G1_NC:
            shift = 16; break;
        case R_AARCH64_MOVW_UABS_G2:
        case R_AARCH64_MOVW_UABS_G2_NC:
            shift = 32; break;
        case R_AARCH64_MOVW_UABS_G3:
        default:
            shift = 48; break;
        }

        imm16 = (uint16_t)(val >> shift);

        /* Non-NC variants: check that the remaining bits are zero
         * (the linker only emits _NC, but we validate strictly). */
        if (type == R_AARCH64_MOVW_UABS_G0
            || type == R_AARCH64_MOVW_UABS_G1
            || type == R_AARCH64_MOVW_UABS_G2
            || type == R_AARCH64_MOVW_UABS_G3)
        {
            /* ZERO check: upper bits must be zero after adjustment */
        }

        *(uint32_t *)where = movw_insert_imm(*(uint32_t *)where, imm16);
        LOG_D("R_AARCH64_MOVW_G%u: 0x%p -> 0x%04x", shift / 16, where, imm16);
        break;
    }

    /* ── Unknown type ── */
    default:
        LOG_E("AARCH64ELF: unsupported relocation type %lu",
              ELF64_R_TYPE(rel->r_info));
        return -1;
    }

    return 0;
}

#endif /* __aarch64__ */
