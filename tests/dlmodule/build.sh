#!/bin/bash
# ==========================================================================
# build.sh — rebuild every dlmodule test .so from source and repack modules.fat
#
# Produces the exact artifacts the QEMU verification (verify_all.sh) consumes:
#   *.so              — AArch64 ET_DYN shared objects
#   modules.fat       — FAT image with all .so files at the root
#
# Requirements:
#   - aarch64-none-elf-gcc (Arm GNU Toolchain) on PATH, or set RTT_EXEC_PATH
#     to the toolchain's bin/ directory.
#   - mkfs.vfat + mcopy (mtools) for the FAT image.
#   - git (optional) to fetch mbedTLS if MBEDTLS_DIR is not set.
#
# Usage:
#   ./build.sh                          # build everything, repack modules.fat
#   MBEDTLS_DIR=/path/to/mbedtls ./build.sh   # use an existing mbedTLS checkout
# ==========================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

# ── Toolchain resolution ────────────────────────────────────────────────
if [ -n "${RTT_EXEC_PATH:-}" ]; then
    TCDIR="$RTT_EXEC_PATH"
else
    TCDIR="$(dirname "$(command -v aarch64-none-elf-gcc 2>/dev/null || echo /nonexistent)")"
fi
CC="${TCDIR}/aarch64-none-elf-gcc"
if [ ! -x "$CC" ]; then
    echo "ERROR: aarch64-none-elf-gcc not found. Set RTT_EXEC_PATH to the toolchain bin/ dir." >&2
    exit 1
fi
echo "== toolchain: $CC"

COMMON="-march=armv8-a -shared -fPIC -nostdlib"
CROSS=""

# ── 1. Simple single-file modules ───────────────────────────────────────
echo "== building simple modules"
for m in mod_minimal mod_ext1 mod_ext2 mod_string_util mod_sensor2 \
         mod_dispatcher mod_allocator; do
    "$CC" $COMMON -O2 -o "$m.so" "$m.c"
    echo "   $m.so"
done

# these two use -O0/-O2 + BIND_NOW per their source comments
"$CC" $COMMON -O0 -Wl,-e,module_init,-z,now -o mod_reloc_full.so mod_reloc_full.c
echo "   mod_reloc_full.so"
"$CC" $COMMON -O2 -Wl,-e,module_init,-z,now -o mod_stress.so mod_stress.c
echo "   mod_stress.so"

# ── 2. Complex kernel export surface consumer ───────────────────────────
"$CC" $COMMON -O2 -o mod_kapi.so mod_kapi.c
echo "   mod_kapi.so"

# ── 2b. RELR compressed-relocation regression (BUGS.md #1) ──────────────
"$CC" $COMMON -O2 -Wl,-z,pack-relative-relocs -o mod_relr.so mod_relr_test.c
echo "   mod_relr.so"

# ── 3. mbedTLS (real open-source library, self-contained .so) ───────────
#    -DMBEDTLS_CONFIG_FILE pins the minimal config; the config routes the
#    library heap (mbedtls_calloc/free) onto rt_calloc/rt_free.
MBEDTLS_DIR="${MBEDTLS_DIR:-}"
if [ -z "$MBEDTLS_DIR" ]; then
    MBEDTLS_DIR="$HERE/.deps/mbedtls"
    if [ ! -f "$MBEDTLS_DIR/include/mbedtls/sha256.h" ]; then
        echo "== fetching mbedTLS 2.28.9 (git clone)"
        rm -rf "$MBEDTLS_DIR"
        git clone --quiet --depth 1 --branch v2.28.9 \
            https://github.com/Mbed-TLS/mbedtls.git "$MBEDTLS_DIR"
    fi
fi
if [ ! -f "$MBEDTLS_DIR/include/mbedtls/sha256.h" ]; then
    echo "ERROR: MBEDTLS_DIR=$MBEDTLS_DIR does not look like an mbedTLS checkout" >&2
    exit 1
fi
echo "== building mod_mbedtls.so (mbedTLS: $MBEDTLS_DIR)"
"$CC" $COMMON -O2 \
    -DMBEDTLS_CONFIG_FILE='"mbedtls_config_min.h"' \
    -I"$HERE" -I"$MBEDTLS_DIR/include" \
    -o mod_mbedtls.so \
    "$MBEDTLS_DIR/library/aes.c" \
    "$MBEDTLS_DIR/library/sha256.c" \
    "$MBEDTLS_DIR/library/platform_util.c" \
    "$MBEDTLS_DIR/library/platform.c" \
    mod_mbedtls_demo.c
echo "   mod_mbedtls.so"

# ── 4. Pack modules.fat ─────────────────────────────────────────────────
echo "== packing modules.fat"
FAT_SIZE_MB="${FAT_SIZE_MB:-8}"
rm -f modules.fat
truncate -s ${FAT_SIZE_MB}M modules.fat
mkfs.vfat modules.fat >/dev/null

# every .so at the FAT root (the kernel mounts this as '/')
for so in *.so; do
    mcopy -i modules.fat "$so" "::/$so"
done
echo "== modules.fat contents:"
mdir -i modules.fat ::/
echo "== done"
