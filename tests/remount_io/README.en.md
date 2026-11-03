# remount_io

[中文](README.md) | [English](README.en.md)

## Purpose

Verify ext2/FAT32 large-file integrity across repeated unmount/remount cycles and confirm that each underlay is revealed after unmount.

## Coverage

- Write and immediately verify one deterministic 1 MiB file per backend.
- Perform three unmount/remount cycles and byte-verify both files after every remount.
- Exercise ext2 direct, single-indirect, and double-indirect blocks, plus a long FAT32 cluster chain.
- Read an xv6 underlay marker after every unmount.

## Oracle

Exact assertions require eight mounts, two successful `keep` runs, six successful `verify` runs, and six underlay reads, for 8 MiB of byte verification. This proves orderly remount integrity, not sudden-power-loss consistency.

## Run

```sh
make remount_io && make run
make loongarch_remount_io && make run
```
