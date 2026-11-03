#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_NAME="public_vfs"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_public_vfs"
fi
RAW_LOG="$LOG_DIR/qemu-public-vfs$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[public_vfs] fs.img not found"
  echo "Use: make public_vfs"
  exit 1
fi

echo "[public_vfs] running QEMU command test"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --expect "mount: ext2 on /mnt dev 2" \
  --expect "test_copy_file(/mnt/hello.c,/hello.c): succeed" \
  --expect "test_copy_file(/mnt/hello.c,/tests/hello.c): succeed" \
  --expect "test_copy_file(/hello.c,/mnt/hello.c): succeed" \
  --expect "test_copy_file(/tests/hello.c,/mnt/hello.c): succeed" \
  --expect "test_cross_fs_copy(): succeed" \
  --expect "PUBLIC_VFS_DONE" \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "mount: failed" \
  --reject "exec cat failed" \
  --reject "exec test1 failed" \
  --reject "exec test2 failed" \
  --reject ": error"
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

echo "[public_vfs] result: $result"
echo "[public_vfs] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
