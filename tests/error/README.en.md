# error

[中文](README.md) | [English](README.en.md)

## Purpose

Verify the stability of VFS error paths: invalid operations must fail cleanly without panicking or corrupting system state.

## Coverage

- Mounting on a nonexistent directory (`mount ext2 /no_such_dir`).
- Rejecting `/.` and `/..` aliases that would cover the native root filesystem.
- With CPUS=3, background and foreground requests are issued against each ext2/FAT32 mount point. The runner leaves a 10 ms scheduling opportunity after a background command, then waits for a stable prompt after the foreground command so later UART input cannot interleave. `mtable.oplock` serializes complete mount calls, and exactly one request per pair succeeds, validating final mutual-exclusion and failure semantics. `MOUNTING` publication, rollback, and unlink visibility are justified by lock-order/source review; the test does not claim to hit that window deterministically.
- After ext2 succeeds, mounting the same type on another directory (`mount ext2 /mnt2`) and overmounting that path with FAT32 (`mount fat32 /mnt`) are both rejected.
- After mounting ext2 and FAT32 normally, a cross-filesystem hard link attempt (`ln /mnt/hello.c /fat/cross.txt`).
- Removing nonexistent files (one on ext2, one on FAT32).

## Key assertions

- expect-count: `mount: failed` occurs exactly seven times, covering the missing directory, two root aliases, both concurrent losers, same-type/different-directory rejection, and cross-type/same-path rejection; each ext2/FAT32 success message occurs exactly once.
- expect: the other error paths fail and `ERROR_PATH_DONE` is reached (proving the system is still running normally).
- reject: `panic:`, `kerneltrap`.

## Links

- The contest's system-stability requirement; B5 (invalid-mount rejection).
