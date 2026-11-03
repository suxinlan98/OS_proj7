#!/usr/bin/env bash
# Run the COMPLETE usertests suite (no -q; includes the six slow tests
# bigdir/manywrites/badwrite/execout/diskfull/outofinodes) on one
# architecture, mirroring the official CPUS=3 QEMU environment.
#
# Usage: bash tests/full_usertests.sh [riscv|loongarch]
#
# This is part of the pre-push gate together with tests/run_all.sh:
# a push must not happen unless both are green.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
ARCH="${1:-riscv}"
LOG_SUFFIX=""
if [ "$ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$ARCH"
fi
LOG="$SCRIPT_DIR/test_baseline/logs/usertests-full$LOG_SUFFIX.log"

mkdir -p "$(dirname "$LOG")"

echo "[full_usertests] building $ARCH kernel and fs.img"
(
  cd "$XV6_DIR" || exit 1
  make ARCH="$ARCH" clean >/dev/null 2>&1
  rm -f fs.img ext2.img fat32.img
  make ARCH="$ARCH" kernel/kernel fs.img
) > /dev/null 2>&1 || {
  echo "[full_usertests] build failed"
  exit 1
}

echo "[full_usertests] running complete usertests on $ARCH (CPUS=3, no -q)"
(
  cd "$XV6_DIR" || exit 1
  ( sleep 8; printf 'usertests\n'; sleep 780 ) | timeout 800 make ARCH="$ARCH" CPUS=3 qemu
) > "$LOG" 2>&1 &
runpid=$!

# The guest verdict appears long before the stdin pipe closes; watch
# the log and stop QEMU as soon as the outcome is known.
for i in $(seq 1 150); do
  if grep -q "ALL TESTS PASSED" "$LOG" 2>/dev/null; then
    break
  fi
  if grep -q "panic:" "$LOG" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$runpid" 2>/dev/null; then
    break
  fi
  sleep 5
done
pkill -f "qemu-system" 2>/dev/null
kill "$runpid" 2>/dev/null
wait "$runpid" 2>/dev/null

if grep -q "ALL TESTS PASSED" "$LOG" && ! grep -q "panic:" "$LOG"; then
  echo "[full_usertests] $ARCH: PASS"
  echo "[full_usertests] log: $LOG"
  exit 0
fi

echo "[full_usertests] $ARCH: FAIL (see $LOG)"
exit 1
