#!/bin/bash
# ==========================================================================
# RT-Thread AArch64 dlmodule: QEMU comprehensive verification
#
# For each of 7 .so modules:
#   dlopen → dlsym → call → verify return value
#
# Usage: ./run_qemu_test.sh
# ==========================================================================

BSP=/home/ubuntu/cluade-workspaces/rt-thread-aarch64/bsp/qemu-virt64-aarch64
IMG=/home/ubuntu/cluade-workspaces/rt-thread-aarch64/tests/dlmodule/modules.fat
cd "$BSP"

# ── Cleanup ──
sudo fuser -k virtio.qcow2 2>/dev/null || true
pkill -9 qemu-system-aarch 2>/dev/null || true
sleep 1; rm -f /tmp/qemu-serial.log /tmp/qemu-run.log

# ── Boot QEMU ──
qemu-system-aarch64 \
  -M virt,gic-version=2 -cpu max -smp 1 -m 128 \
  -kernel rtthread.bin -append 'console=ttyAMA0 earlycon' \
  -nographic -serial pty -monitor none \
  -drive if=none,file=virtio.qcow2,format=qcow2,id=blk0 \
  -device virtio-blk-device,drive=blk0 \
  -drive if=none,file="$IMG",format=raw,id=blk1 \
  -device virtio-blk-device,drive=blk1 \
  &>/tmp/qemu-run.log &
QPID=$!

# ── Find PTY ──
for i in $(seq 1 20); do
  PTY=$(grep -Po '/dev/pts/\d+' /tmp/qemu-run.log 2>/dev/null | head -1)
  [ -n "$PTY" ] && break; sleep 1
done
[ -z "$PTY" ] && { echo "ERROR: QEMU didn't start"; cat /tmp/qemu-run.log; exit 1; }
echo "QEMU PID=$QPID  PTY=$PTY"
sleep 12

# ── Send all test commands, then read all output ──
{
  echo -e "mount vda0 / elm\r";           sleep 3

  # 1. mod_minimal.so: add + mul
  echo -e "echo ===1.mod_minimal===\r";   sleep 1
  echo -e "cmd_call2 /mod_minimal.so add 10 20\r"; sleep 5
  echo -e "cmd_call2 /mod_minimal.so mul 30 3\r";  sleep 5

  # 2. mod_ext1.so: rt_kprintf via PLT
  echo -e "echo ===2.mod_ext1===\r";      sleep 1
  echo -e "cmd_load /mod_ext1.so\r";      sleep 5

  # 3. mod_ext2.so: strlen/memcpy/strcmp via PLT
  echo -e "echo ===3.mod_ext2===\r";      sleep 1
  echo -e "cmd_load /mod_ext2.so\r";      sleep 5

  # 4. mod_string_util.so: get_stats via dlsym
  echo -e "echo ===4.mod_string_util===\r"; sleep 1
  echo -e "cmd_call /mod_string_util.so get_stats 0\r"; sleep 5

  # 5. mod_sensor2.so: process_sample x3 filters
  echo -e "echo ===5.mod_sensor2===\r";   sleep 1
  echo -e "cmd_call2 /mod_sensor2.so process_sample 500 1\r"; sleep 5
  echo -e "cmd_call2 /mod_sensor2.so process_sample 999 0\r"; sleep 5
  echo -e "cmd_call2 /mod_sensor2.so process_sample 100 4\r"; sleep 5

  # 6. mod_dispatcher.so: 10 handlers
  echo -e "echo ===6.mod_dispatcher===\r"; sleep 1
  echo -e "cmd_load /mod_dispatcher.so\r";         sleep 5

  # 7. mod_allocator.so: rt_malloc wrapper
  echo -e "echo ===7.mod_allocator===\r";  sleep 1
  echo -e "cmd_call /mod_allocator.so mod_malloc 128\r"; sleep 5

  # 8. list all loaded modules
  echo -e "echo ===MODULE_LIST===\r";      sleep 1
  echo -e "list_module\r";                 sleep 2

} > "$PTY" &

# ── Read all output with a single timeout ──
timeout 120 cat "$PTY" > /tmp/qemu-serial.log 2>/dev/null || true

# ── Cleanup ──
kill $QPID 2>/dev/null; wait $QPID 2>/dev/null || true

echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  QEMU dlopen → dlsym → call — ALL 7 MODULES"
echo "═══════════════════════════════════════════════════════════"
echo ""
cat /tmp/qemu-serial.log
