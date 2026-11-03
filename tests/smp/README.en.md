# smp

[中文](README.md) | [English](README.en.md)

## Purpose

Apply true multi-core (CPUS=3) concurrency pressure to both file-system backends, validating backend allocator serialization (C1 fix), the FAT32 chain-walk cache, and the unmount lifecycle.

## Coverage

- Three-way concurrency on ext2 and on FAT32: two background `bigfile` writers plus one foreground `bigfile`, each writing 1 MiB, followed by byte-by-byte `verify` of every file.
- `stressfs` inside each mount point (4 writers + 1 reader concurrently creating/writing/unlinking).
- `umountstress` first proves that an open file keeps a mount busy, then interleaves lookup with repeated `mount/umount` across four child processes.
- Pre-fix baseline: the FAT32 section deterministically hung in this scenario (C1 red evidence).

## Key assertions

- expect: both mount strings; the full `bigfile: ok` sequence (keep/full/verify); `stressfs starting` ×2; `UMOUNT_STRESS_OK`; `SMP_DONE`.
- reject: `panic:`, `kerneltrap`, `mount: failed`, `bigfile: FAIL`, `umountstress: FAIL`, `cannot`.
- The runner uses `--cpus 3 --prompt-wait --step-wait 0.01`: background writers are separated by only 10 ms to preserve overlap, while foreground commands use quiescence plus prompt detection to prevent premature input.

## Links

- C1 backend allocator SMP serialization; the FAT32 chain-walk O(n²)→O(n) fix; unmount/path-walk concurrency; the contest's stability requirement.
