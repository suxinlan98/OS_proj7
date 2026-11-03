#!/usr/bin/env bash
set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
fi

TEST_LOG="$LOG_DIR/usertests-quick$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result$LOG_SUFFIX.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[test_baseline] fs.img not found"
  echo "Use: make test_baseline"
  exit 1
fi

echo "[test_baseline] running usertests -q on $TEST_ARCH"
(
  cd "$XV6_DIR" || exit 1
  # Hard time limit: if the guest panics, test-xv6.py blocks forever on
  # an empty output pipe; turn that into a visible FAIL instead.
  timeout 420 python3 test-xv6.py usertests -q
) 2>&1 | tee "$TEST_LOG"
test_status=$?

if [ "$test_status" -eq 0 ] && grep -q "ALL TESTS PASSED" "$TEST_LOG"; then
  result="PASS"
else
  result="FAIL"
fi

{
  echo "Result: $result"
  echo "Test Status: $test_status"
  echo "Test Log: $TEST_LOG"
} > "$RESULT_LOG"

echo "[test_baseline] result: $result"
echo "[test_baseline] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
