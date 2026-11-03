# mount_storm

[中文](README.md) | [English](README.en.md)

## Purpose

Race path lookup/open against mount/unmount topology changes with CPUS=3, exercising reader pins, inode-reference busy detection, and four-state publication.

## Coverage

- Parameterized `umountstress` runs the same workload on ext2 and FAT32.
- Each backend uses two lookup workers and two topology workers at 256 iterations per worker.
- Both backends total 2,048 concurrent worker iterations; an open file is first checked to make unmount deterministically busy.
- Probe files are read after stress to verify a usable final topology and intact data.

## Oracle

Both backend-specific `UMOUNT_STRESS_OK` markers are required. Panic, trap, mount failure, worker failure, and final read failure are rejected. Guest ticks are reported for reproducibility without a machine-dependent pass threshold.

## Run

```sh
make mount_storm && make run
make loongarch_mount_storm && make run
```
