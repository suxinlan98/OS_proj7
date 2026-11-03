# persist

[中文](README.md) | [English](README.en.md)

## Purpose

Verify that writes to ext2 and FAT32 truly land in `fs.img`: after writing, QEMU is shut down and restarted, the file systems are remounted, and the contents are read back and verified byte by byte. This is the difference between a real file system and an in-memory demo.

## Coverage

- First session (write.cmds.sh): mount both backends, write `persist.txt`/`p.txt` and read them back immediately; write a 1 MiB double-indirect file via `bigfile /mnt/bigfile keep`.
- Restart QEMU (same image, no rebuild).
- Second session (read.cmds.sh): remount, read all files back; verify the 1 MiB file byte by byte with `bigfile verify`; re-check the externally injected large file with `wc`.

## Key assertions

- expect: mount strings in both sessions; `persist-ext2`/`persist-fat` read back; `bigfile: ok (1048576 bytes, keep)` and `(..., verify)`; `0 1 1048576`; both DONE markers.
- reject: `panic:`, `kerneltrap`, `mount: failed`, `bigfile: FAIL`, and the open-failure strings.

## Links

- Contest stability and persistence requirements; D1 cross-boot consistency for double-indirect files.
