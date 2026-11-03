# mount_lifecycle

[中文](README.md) | [English](README.en.md)

## Purpose

Deterministically verify the `mount/umount` state machine, failure rollback, mount-point protection, and nested topology semantics.

## Coverage

- Reject unmounting a missing path, `/`, `/.`, and `/..`.
- Force ext2/FAT32 backend initialization failures with the wrong device, then mount correctly to prove slot rollback.
- Reject removal of mounted directories and reject parent unmount while a child mount exists.
- Reveal the ext2 underlay after child unmount and the xv6 underlay after parent unmount.
- Reaccess data after remount and move the FAT32 instance from a nested path to `/fat`.

## Oracle

The runner uses exact counts for five invalid/busy unmounts, two mount failures, two protected-directory removals, and four visibility markers. `panic:`, `kerneltrap`, and read failures are rejected.

## Run

```sh
make mount_lifecycle && make run
make loongarch_mount_lifecycle && make run
```
