#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_NAME="error"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_error"
fi
RAW_LOG="$LOG_DIR/qemu-error$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[error] fs.img not found"
  echo "Use: make error"
  exit 1
fi

echo "[error] running QEMU command test"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --cpus 3 \
  --prompt-wait \
  --step-wait 0.01 \
  --expect-count "mount: failed" 7 \
  --expect-count "mount: ext2 on /mnt dev 2" 1 \
  --expect-count "mount: fat32 on /fat dev 3" 1 \
  --expect "link /mnt/hello.c /fat/cross.txt: failed" \
  --expect "rm: /mnt/no_such_file failed to delete" \
  --expect "rm: /fat/no_such.txt failed to delete" \
  --expect "ERROR_PATH_DONE" \
  --reject "panic:" \
  --reject "kerneltrap"
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

echo "[error] result: $result"
echo "[error] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
