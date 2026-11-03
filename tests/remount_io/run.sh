#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_NAME="remount_io"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_remount_io"
fi
RAW_LOG="$LOG_DIR/qemu-remount-io$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result$LOG_SUFFIX.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[remount_io] fs.img not found"
  echo "Use: make remount_io"
  exit 1
fi

echo "[remount_io] running repeated 1 MiB remount verification"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --prompt-wait \
  --cmd-timeout 300 \
  --final-wait 300 \
  --expect-count "mount: ext2 on /mnt dev 2" 4 \
  --expect-count "mount: fat32 on /fat dev 3" 4 \
  --expect-count "bigfile: ok (1048576 bytes, keep)" 2 \
  --expect-count "bigfile: ok (1048576 bytes, verify)" 6 \
  --expect-count "EXT2_LOWER_LAYER" 4 \
  --expect-count "FAT_LOWER_LAYER" 4 \
  --expect "REMOUNT_IO_DONE" \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "mount: failed" \
  --reject "umount: busy or invalid" \
  --reject "bigfile: FAIL" \
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
  echo "Validated Bytes: 8388608"
  echo "Command File: $SCRIPT_DIR/cmds.sh"
  echo "Raw Log: $RAW_LOG"
} > "$RESULT_LOG"

echo "[remount_io] result: $result"
echo "[remount_io] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
