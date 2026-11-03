#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_NAME="fat32"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_fat32"
fi
RAW_LOG="$LOG_DIR/qemu-fat32$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[fat32] fs.img not found"
  echo "Use: make fat32"
  exit 1
fi

echo "[fat32] running QEMU command test"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --expect "mount: fat32 on /fat dev 3" \
  --expect "fat32-one" \
  --expect "fat32-two" \
  --expect "fat-nested" \
  --expect "fat32-lfn-ok" \
  --expect "longfilename.t" \
  --expect "ShortLong.txt" \
  --expect "FAT32_DONE" \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "mount: failed" \
  --reject "failed to delete" \
  --reject "open /fat/a.txt failed" \
  --reject "mkdir: /fat/d1 failed" \
  --reject "open /fat/d1/d2/f.txt failed" \
  --reject "LONGFI~1" \
  --reject "open /fat/longfilename.txt failed" \
  --reject "open /fat/shortlong.txt failed"
test_status=$?

if [ "$test_status" -eq 0 ]; then
  result="PASS"
else
  result="FAIL"
fi

{
  echo "Result: $result"
  echo "Test Status: $test_status"
  echo "Command File: $SCRIPT_DIR/cmds.sh"
  echo "Raw Log: $RAW_LOG"
} > "$RESULT_LOG"

echo "[fat32] result: $result"
echo "[fat32] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
