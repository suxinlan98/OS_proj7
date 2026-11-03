#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
TEST_CPUS="${TEST_CPUS:-3}"
LOG_SUFFIX=""
CASE_NAME="smp"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_smp"
fi
RAW_LOG="$LOG_DIR/qemu-smp$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result$LOG_SUFFIX.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[smp] fs.img not found"
  echo "Use: make smp"
  exit 1
fi

echo "[smp] running SMP concurrency test on $TEST_ARCH with CPUS=$TEST_CPUS"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --cpus "$TEST_CPUS" \
  --prompt-wait \
  --step-wait 0.01 \
  --final-wait 300 \
  --expect "mount: ext2 on /mnt dev 2" \
  --expect "mount: fat32 on /fat dev 3" \
  --expect "bigfile: ok (1048576 bytes, full)" \
  --expect "bigfile: ok (1048576 bytes, verify)" \
  --expect "stressfs starting" \
  --expect "UMOUNT_STRESS_OK" \
  --expect "SMP_DONE" \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "mount: failed" \
  --reject "bigfile: FAIL" \
  --reject "umountstress: FAIL" \
  --reject "cannot"
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
  echo "Command File: $SCRIPT_DIR/cmds.sh"
  echo "Raw Log: $RAW_LOG"
} > "$RESULT_LOG"

echo "[smp] result: $result"
echo "[smp] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
