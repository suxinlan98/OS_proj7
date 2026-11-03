#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_PREFIX="persist"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_PREFIX="${TEST_ARCH}_persist"
fi
WRITE_LOG="$LOG_DIR/qemu-persist-write$LOG_SUFFIX.log"
READ_LOG="$LOG_DIR/qemu-persist-read$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[persist] fs.img not found"
  echo "Use: make persist"
  exit 1
fi

echo "[persist] first QEMU session: write files"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_PREFIX-write" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/write.cmds.sh" \
  --log "$WRITE_LOG" \
  --expect "mount: ext2 on /mnt dev 2" \
  --expect "mount: fat32 on /fat dev 3" \
  --expect "persist-ext2" \
  --expect "persist-fat" \
  --expect "bigfile: ok (1048576 bytes, keep)" \
  --expect "PERSIST_WRITE_DONE" \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "mount: failed" \
  --reject "bigfile: FAIL" \
  --reject "open /mnt/persist.txt failed" \
  --reject "open /fat/p.txt failed" \
  --final-wait 150
write_status=$?

if [ "$write_status" -eq 0 ]; then
  echo "[persist] second QEMU session: read files after remount"
  python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
    --case "$CASE_PREFIX-read" \
    --xv6-dir "$XV6_DIR" \
    --commands "$SCRIPT_DIR/read.cmds.sh" \
    --log "$READ_LOG" \
    --expect "mount: ext2 on /mnt dev 2" \
    --expect "mount: fat32 on /fat dev 3" \
    --expect "persist-ext2" \
    --expect "persist-fat" \
    --expect "bigfile: ok (1048576 bytes, verify)" \
    --expect "0 1 1048576" \
    --expect "PERSIST_READ_DONE" \
    --reject "panic:" \
    --reject "kerneltrap" \
    --reject "mount: failed" \
    --reject "bigfile: FAIL" \
    --reject "open /mnt/persist.txt failed" \
    --reject "open /fat/p.txt failed" \
    --final-wait 150
  read_status=$?
else
  read_status=1
fi

if [ "$write_status" -eq 0 ] && [ "$read_status" -eq 0 ]; then
  result="PASS"
else
  result="FAIL"
fi

{
  echo "Result: $result"
  echo "Write Status: $write_status"
  echo "Read Status: $read_status"
  echo "Write Commands: $SCRIPT_DIR/write.cmds.sh"
  echo "Read Commands: $SCRIPT_DIR/read.cmds.sh"
  echo "Write Log: $WRITE_LOG"
  echo "Read Log: $READ_LOG"
} > "$RESULT_LOG"

echo "[persist] result: $result"
echo "[persist] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
