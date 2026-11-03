# tests

[中文](README.md) | [English](README.en.md)

This directory holds the project's host-side test infrastructure: test cases, driver scripts, entry points, and passing evidence.  The guest-side `/tests` directory content (official test-case data files) lives in `src/tests/`; the two serve different roles — see the root `README.md`.

## Layout

```text
tests/
|-- README.md / README.en.md     this file (bilingual)
|-- MATRIX.md                    test coverage matrix
|-- VFS_TESTS_PIN.txt            pinned upstream VFS test version
|-- check_env.sh                 environment self-check
|-- run_all.sh                   one-shot 22-case dual-arch regression
|-- full_usertests.sh            complete usertests (without -q)
|-- runner/
|   `-- run_qemu_case.py         QEMU test driver
|-- evidence/                    archived passing evidence (README + raw logs)
|-- test_baseline/               official tests A: usertests (quick/full)
|-- public_vfs/                  official tests B: public VFS flow replica
|-- ext2/                        ext2 surface (double-indirect, long names)
|-- fat32/                       fat32 surface (incl. LFN long names)
|-- error/                       error-path stability
|-- semantics/                   mount/link/unlink semantics
|-- persist/                     two-session persistence
|-- smp/                         CPUS=3 file-I/O concurrency stress
|-- mount_lifecycle/             state machine, nested topology, rollback
|-- remount_io/                  repeated dual-backend 1 MiB verification
`-- mount_storm/                 CPUS=3 dual-backend mount storm
```

Each test-point directory follows a uniform kit: `README.md` (bilingual four-section doc) + `*.cmds.sh` (commands fed to the xv6 shell) + `run.sh` (expect/reject assertions) + `logs/` (run products, not tracked).

## Test Points

- `test_baseline`: stock bundled tests (contest task 3), `usertests -q`; the complete run is handled by `full_usertests.sh`.
- `public_vfs`: byte-faithful replica of the official VFS evaluation flow (contest task 4).
- `ext2`: mount/read/write/unlink/nested dirs/long-name alias/1 MiB double-indirect files/external big files.
- `fat32`: mount/8.3 operations/nested dirs/three LFN access paths.
- `error`: invalid mounts, cross-filesystem links, removing missing files — no panic allowed.
- `semantics`: mount-root `..`, ext2 hard links, FAT32 link rejection, unlinkopen, and umount busy/success/underlay/remount semantics.
- `persist`: write → reboot → remount → byte-exact read-back (incl. a 1 MiB double-indirect file).
- `smp`: CPUS=3 three-way concurrent bigfile + stressfs inside mount points + lookup/mount/umount races (C1 and unmount-lifecycle regression).
- `mount_lifecycle`: invalid unmounts, backend rollback, mount-point protection, nested-mount busy, underlay recovery, and remount at another mount point.
- `remount_io`: one 1 MiB file per backend, followed by three unmount/remount cycles and byte-exact verification.
- `mount_storm`: two lookup workers and two topology workers run 256 iterations per backend with CPUS=3.

## Running

Two-step workflow from the repository root:

```sh
make <name> && make run        # name: a test point (e.g. ext2, smp)
make loongarch_<name> && make run
```

Aggregate entry points:

```sh
bash tests/run_all.sh          # 11 test points x 2 architectures = 22 runs
bash tests/full_usertests.sh riscv
bash tests/full_usertests.sh loongarch
make verify-all                # env check + everything above
```

## Pre-push Gate

All of the following must be green before every push to the remote:

```sh
bash tests/run_all.sh                      # 22/22 on both architectures
bash tests/full_usertests.sh riscv         # complete usertests, no -q
bash tests/full_usertests.sh loongarch
```

## Conventions

1. Command files: single-session test points use `cmds.sh`; multi-session ones use `<phase>.cmds.sh` (e.g. `write.cmds.sh`/`read.cmds.sh`).
2. Assertion style: `--expect` lists success markers, `--expect-count` proves exact transition counts, and `--reject` lists failure/abnormality strings; every test point includes `panic:`/`kerneltrap` negative assertions.
3. Cases using `--prompt-wait` wait for a stable shell prompt before the first and subsequent commands; a prompt timeout is a hard failure, preventing UART input loss from being misclassified as a kernel defect.
4. `logs/` holds local run products and is not tracked; final passing evidence goes to `evidence/`.
5. Adding a test point = new directory (the kit) + root Makefile target + registration in `run_all.sh` + a new row in `MATRIX.md`.
