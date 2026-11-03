#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
TEST_CPUS="${TEST_CPUS:-3}"
LOG_SUFFIX=""
CASE_NAME="mount_storm"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_mount_storm"
fi
RAW_LOG="$LOG_DIR/qemu-mount-storm$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result$LOG_SUFFIX.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[mount_storm] fs.img not found"
  echo "Use: make mount_storm"
  exit 1
fi

echo "[mount_storm] running CPUS=$TEST_CPUS dual-backend topology stress"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --cpus "$TEST_CPUS" \
  --prompt-wait \
  --step-wait 0.01 \
  --cmd-timeout 600 \
  --final-wait 600 \
  --expect "UMOUNT_STRESS_OK ext2 256 loops" \
  --expect "UMOUNT_STRESS_OK fat32 256 loops" \
  --expect-count "EXT2_STORM_PROBE" 2 \
  --expect-count "FAT_STORM_PROBE" 2 \
  --expect "MOUNT_STORM_DONE" \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "mount: failed" \
  --reject "umountstress: FAIL" \
  --reject "cat: cannot open"
test_status=$?

if [ "$test_status" -eq 0 ]; then
  result="PASS"
else
  result="FAIL"
fi

{
  echo "Result: $result"
  echo "Test Status: $test_status"
  echo "CPUs: $TEST_CPUS"
  echo "Loops per Worker and Backend: 256"
  echo "Concurrent Worker Iterations: 2048"
  echo "Command File: $SCRIPT_DIR/cmds.sh"
  echo "Raw Log: $RAW_LOG"
} > "$RESULT_LOG"

echo "[mount_storm] result: $result"
echo "[mount_storm] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
