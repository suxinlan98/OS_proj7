#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_NAME="fault"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_error"
fi
RAW_LOG="$LOG_DIR/qemu-fault$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[fault] fs.img not found"
  echo "Use: make fault"
  exit 1
fi

echo "[fault] running QEMU command test"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --cpus 3 \
  --cmd-timeout 240 \
  --final-wait 120 \
  --prompt-wait \
  --step-wait 0.01 \
  --expect "fsfault: file system server is a separate process: OK" \
  --expect "fsfault: init restarted the file system server: OK" \
  --expect "fsfault: pipes/fork work while file system is down: OK" \
  --expect "fsfault: stale open file is rejected: OK" \
  --expect "fsfault: data committed before the crash survived: OK" \
  --expect "fsfault: recovered from a crash inside the server: OK" \
  --expect "fsfault: survived 6 server crashes under load: OK" \
  --expect "fsfault: remount ext2 after restart: OK" \
  --expect "FSFAULT_DONE" \
  --expect "FAULT_PATH_DONE" \
  --reject ": FAIL" \
  --reject "FSFAULT_FAIL" \
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

echo "[fault] result: $result"
echo "[fault] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
