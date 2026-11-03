# fat32

[中文](README.md) | [English](README.en.md)

## Purpose

Verify the FAT32 backend through the VFS: mount, 8.3 short-name file operations, empty files, nested directories, and FAT32 long-file-name (LFN) reading and directory listing.

## Coverage

- Explicit `mount fat32 /fat`.
- 8.3 short names: create/overwrite/unlink a regular file, empty-file creation, nested `mkdir d1/d2`.
- LFN reads (fixture files injected into the image by mkfat32): `cat /fat/longfilename.txt` (16-char full name, hits the truncated alias via the namei layer); `cat /fat/shortlong.txt` (lowercase query matching a mixed-case LFN, case-insensitive); `cat /fat/longfi~1.txt` (8.3 mangled name compatibility).
- `ls /fat` shows assembled long names (`longfilename.t` truncated, `ShortLong.txt` intact) instead of the mangled 8.3 forms.

## Key assertions

- expect: `mount: fat32 on /fat dev 3`; content strings; `fat32-lfn-ok`; `longfilename.t`; `ShortLong.txt`.
- reject: `panic:`, `kerneltrap`, `mount: failed`, `LONGFI~1` (the mangled name must not appear in ls), and the open/mkdir failure strings.

## Links

- Contest task 1 (FAT32 support); D4 (LFN read support).
