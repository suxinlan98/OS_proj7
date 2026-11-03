#!/usr/bin/env bash
# Run every test point on both architectures, in sequence.
# Usage: bash tests/run_all.sh
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SUITES="test_baseline public_vfs ext2 fat32 error semantics persist smp \
mount_lifecycle remount_io mount_storm \
loongarch_baseline loongarch_public_vfs loongarch_ext2 loongarch_fat32 \
loongarch_error loongarch_semantics loongarch_persist loongarch_smp \
loongarch_mount_lifecycle loongarch_remount_io loongarch_mount_storm"

total=0
failed=0
for t in $SUITES; do
  total=$((total + 1))
  echo "===== $t : $(date -Iseconds) ====="
  if make "$t" >/dev/null 2>&1 && make run >/dev/null 2>&1; then
    st=PASS
  else
    st=FAIL
    failed=1
  fi
  printf "%-24s %s\n" "$t" "$st"
done

if [ "$failed" -ne 0 ]; then
  echo "run_all: FAIL"
  exit 1
fi
echo "run_all: PASS ($total/$total)"
