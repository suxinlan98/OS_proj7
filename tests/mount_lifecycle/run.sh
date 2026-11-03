#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_NAME="mount_lifecycle"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_mount_lifecycle"
fi
RAW_LOG="$LOG_DIR/qemu-mount-lifecycle$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result$LOG_SUFFIX.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[mount_lifecycle] fs.img not found"
  echo "Use: make mount_lifecycle"
  exit 1
fi

echo "[mount_lifecycle] running lifecycle and nested-mount test"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --prompt-wait \
  --cmd-timeout 180 \
  --expect-count "umount: busy or invalid" 5 \
  --expect-count "mount: failed" 2 \
  --expect-count "rm: /mnt failed to delete" 1 \
  --expect-count "rm: /mnt/nest failed to delete" 1 \
  --expect-count "LOWER_MNT_VISIBLE" 2 \
  --expect-count "LOWER_FAT_VISIBLE" 2 \
  --expect-count "EXT2_CHILD_VISIBLE" 3 \
  --expect-count "FAT_NESTED_PERSIST" 3 \
  --expect "umount: /mnt/nest" \
  --expect "umount: /mnt" \
  --expect "umount: /fat" \
  --expect "MOUNT_LIFECYCLE_DONE" \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "cat: cannot open" \
  --reject "mkdir: /mnt/nest failed to create"
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

echo "[mount_lifecycle] result: $result"
echo "[mount_lifecycle] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
