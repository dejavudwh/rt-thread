#!/bin/bash
# ==========================================================================
# verify_all.sh — RT-Thread AArch64 dlmodule: QEMU end-to-end verification
#
# Boots the qemu-virt64-aarch64 BSP with modules.fat as the root block device,
# then drives the msh console through every dlmodule test:
#
#   simple modules   (add/mul, string ops, allocator, sensor/dispatcher tables)
#   mod_kapi.so      (complex kernel export surface: data symbols, const LUT,
#                     function-pointer struct, IPC, callback)
#   mod_mbedtls.so   (real open-source library: SHA-256 + AES-128-CBC)
#   mod_stress.so    (ring buffer + filter dispatch + CRC + allocator)
#
# Usage:
#   ./build.sh          # first rebuild the .so files + modules.fat
#   ./verify_all.sh
#
# The BSP directory is located relative to this script (two levels up:
# tests/dlmodule -> rt-thread).  Override with RTT_BSP if needed.
# ==========================================================================
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BSP="${RTT_BSP:-$HERE/../../bsp/qemu-virt64-aarch64}"
IMG="$HERE/modules.fat"

QEMU="qemu-system-aarch64"

# ── Sanity checks ────────────────────────────────────────────────────────
[ -f "$BSP/rtthread.bin" ] || { echo "ERROR: rtthread.bin not found in $BSP (run build.sh + scons first)"; exit 1; }
[ -f "$IMG" ]            || { echo "ERROR: $IMG not found (run build.sh first)"; exit 1; }
command -v "$QEMU" >/dev/null || { echo "ERROR: $QEMU not found"; exit 1; }

# ── Cleanup any stale QEMU holding the image ──────────────────────────────
pkill -9 "$QEMU" 2>/dev/null || true
sleep 1
rm -f /tmp/dlmodule-verify.log

# ── Boot QEMU, drive msh via stdio, capture everything ───────────────────
# Commands are fed on stdin after a delay to let the kernel reach msh.
# -smp 4 matches RT_CPUS_NR in the BSP config; -cpu max + gic-version=2 is the
# BSP's default qemu.py configuration.
{
  sleep 12

  # mount modules.fat (attached as the virtio block device) at root
  printf 'mount vda0 / elm\r'; sleep 3

  # ── simple modules ──
  printf 'cmd_call2 /mod_minimal.so add 10 20\r';   sleep 3
  printf 'cmd_call2 /mod_minimal.so mul 30 3\r';    sleep 3
  printf 'cmd_load  /mod_ext2.so\r';                sleep 3
  printf 'cmd_call  /mod_string_util.so get_stats 0\r'; sleep 3
  printf 'cmd_call2 /mod_sensor2.so process_sample 500 1\r'; sleep 3
  printf 'cmd_load  /mod_dispatcher.so\r';          sleep 3
  printf 'cmd_call  /mod_allocator.so mod_malloc 128\r'; sleep 3

  # ── complex kernel export surface ──
  printf 'cmd_kapi\r'; sleep 6

  # ── real open-source library ──
  printf 'cmd_mbedtls\r'; sleep 8

  # ── stress module ──
  printf 'cmd_load  /mod_stress.so\r';              sleep 3
  printf 'cmd_call  /mod_stress.so stress_quick_check 0\r'; sleep 4

  # ── RELR compressed-relocation regression (BUGS.md #1) ──
  printf 'cmd_call2 /mod_relr.so relr_add 10 20\r'; sleep 4
  printf 'cmd_call2 /mod_relr.so relr_sub 100 37\r'; sleep 4
  printf 'cmd_call2 /mod_relr.so relr_mul 6 7\r';   sleep 4

  # ── list loaded modules ──
  printf 'list_module\r'; sleep 2
  sleep 2
} | timeout 120 "$QEMU" -M virt,gic-version=2 -cpu max -smp 4 -m 128 \
    -kernel "$BSP/rtthread.bin" -append 'console=ttyAMA0 earlycon' \
    -serial stdio -display none -monitor none \
    -drive if=none,file="$IMG",format=raw,id=blk0 \
    -device virtio-blk-device,drive=blk0 \
    > /tmp/dlmodule-verify.log 2>&1

# ── Print a filtered summary of the key pass/fail lines ──────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  AArch64 dlmodule — QEMU verification results"
echo "═══════════════════════════════════════════════════════════════"
echo ""

grep -a -E "=> |ALL CHECKS PASSED|PASS|FAIL|FAILED|dlsym|module loaded|mount .* succeed|callback|semaphore|SHA-256|AES-128" \
    /tmp/dlmodule-verify.log || true

echo ""
echo "── full log: /tmp/dlmodule-verify.log ──"
