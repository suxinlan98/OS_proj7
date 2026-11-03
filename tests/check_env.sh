#!/usr/bin/env bash
# Environment check for building and testing this project.
# Verifies host tools, both cross toolchains, and QEMU emulators.
# Usage: bash tests/check_env.sh
set -u

missing=0

check() {
  # $1 = description, rest = command candidates (first found wins)
  desc="$1"; shift
  found=""
  for c in "$@"; do
    if command -v "$c" >/dev/null 2>&1; then
      found="$c"
      break
    fi
  done
  if [ -n "$found" ]; then
    printf "[OK]      %-28s %s\n" "$desc" "$found"
  else
    printf "[MISSING] %-28s (tried: %s)\n" "$desc" "$*"
    missing=1
  fi
}

echo "== host build/test tools =="
check "make"        make
check "host gcc"    gcc
check "python3"     python3
check "mkfs.ext2"   mkfs.ext2
check "debugfs"     debugfs

echo "== RISC-V toolchain =="
check "riscv gcc"   riscv64-unknown-elf-gcc riscv64-linux-gnu-gcc riscv64-unknown-linux-gnu-gcc
check "riscv objdump" riscv64-unknown-elf-objdump riscv64-linux-gnu-objdump riscv64-unknown-linux-gnu-objdump

echo "== LoongArch64 toolchain =="
check "loongarch gcc" loongarch64-linux-gnu-gcc loongarch64-linux-gnu-gcc-14 loongarch64-unknown-linux-gnu-gcc
check "loongarch objdump" loongarch64-linux-gnu-objdump loongarch64-unknown-linux-gnu-objdump

echo "== QEMU =="
check "qemu riscv64"    qemu-system-riscv64
check "qemu loongarch64" qemu-system-loongarch64

if [ "$missing" -ne 0 ]; then
  echo
  echo "environment incomplete; see Dockerfile for the reference setup"
  exit 1
fi
echo
echo "environment OK"
