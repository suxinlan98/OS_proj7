# evidence

[中文](README.md) | [English](README.en.md)

This directory archives final passing evidence for key runs (raw QEMU output, fsck output, and summaries). `make verify-all` reruns the dual-architecture functional regression and full usertests, but does not overwrite this directory; fsck and performance evidence use the separate commands below.

## Contents

- `run_all.txt` — full regression summary (11 test points × 2 architectures = 22 runs)
- `usertests-full-riscv.txt` — RISC-V complete usertests raw output (without `-q`, including the six slow tests)
- `usertests-full-loongarch.txt` — LoongArch64 complete usertests output
- `smp-riscv.txt` / `smp-loongarch.txt` — CPUS=3 concurrency stress raw output (three-way concurrent 1 MiB writes + verification + stressfs + lookup/mount/umount races)
- `error-riscv.txt` / `error-loongarch.txt` — raw QEMU output for the final mount-conflict and error-path runs
- `semantics-riscv.txt` / `semantics-loongarch.txt` — raw QEMU output for cross-backend ELF execution, mount-root `..`, links, unlink-while-open, safe unmount, and remount semantics
- `mount-lifecycle-riscv.txt` / `mount-lifecycle-loongarch.txt` — rollback, nested mounts, mount-point protection, and underlay recovery
- `remount-io-riscv.txt` / `remount-io-loongarch.txt` — repeated dual-backend 1 MiB remount and byte verification
- `mount-storm-riscv.txt` / `mount-storm-loongarch.txt` — raw CPUS=3 dual-backend mount/unmount stress output
- `e2fsck-final.txt` — five-pass read-only `e2fsck -fn` output for the final ext2 test image
- `bench-riscv.txt` — historical RISC-V vfsbench numbers captured before the current P0 fixes; they do not represent final-source performance
- `bench-loongarch.txt` — retained for audit, but its contents were mistakenly captured from RISC-V and must not be cited as LoongArch evidence; a fresh run is required
- `bench-rev5-baseline.txt` — pristine rev5 under the workload paired with the historical record above
- `bench-fat32-before-opts.txt` / `bench-fat32-nextfree-only.txt` — pre-P0 FAT32 allocation/walk states before and between the two optimizations

## Reproducing functional regression

```sh
make verify-all
```

or step by step:

```sh
bash tests/check_env.sh
bash tests/run_all.sh
bash tests/full_usertests.sh riscv
bash tests/full_usertests.sh loongarch
make smp && make run
make loongarch_smp && make run
```

Each focused run writes raw QEMU output under `tests/<case>/logs/`. Archive a log here only after its result has been checked, so an interrupted or failed run cannot replace final evidence.

Read-only ext2 consistency check:

```sh
e2fsck -fn src/ext2.img
```

See `docs/contest/performance.md` for benchmark workloads, commands, and the current evidence boundary. `make verify-all` does not run `vfsbench`.
