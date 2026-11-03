# public_vfs

[中文](README.md) | [English](README.en.md)

## Purpose

Byte-for-byte reproduction of the official VFS evaluation flow: boot from the xv6 file system, manually mount ext2 at `/mnt`, then run the official `test1`/`test2` cross-filesystem copy cases via `cd tests; cat cmds.sh | sh`.Maps to contest task 4. The pinned upstream version of the official test files is recorded in `tests/VFS_TESTS_PIN.txt`.

## Coverage

- Explicit `mount ext2 /mnt` (no auto-mount).
- The image's `/tests` layout matches the official reference (cmds.sh, hello.c, test1, test2, hello).
- test1: four-direction regular-file copies between ext2 and xv6.
- test2: xv6→ext2 copy with read-back content verification.
- exec root fallback for bare command names (running `/cat` etc. from /tests).

## Key assertions

- expect: `mount: ext2 on /mnt dev 2`; the four `test_copy_file(...): succeed` lines; `test_cross_fs_copy(): succeed`; `PUBLIC_VFS_DONE`.
- reject: `panic:`, `kerneltrap`, `mount: failed`, `exec cat failed`, `exec test1 failed`, `exec test2 failed`, `: error`.

## Links

- Contest task 4.
- Official test origin and pin: `tests/VFS_TESTS_PIN.txt`.
