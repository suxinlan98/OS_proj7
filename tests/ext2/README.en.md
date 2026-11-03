# ext2

[中文](README.md) | [English](README.en.md)

## Purpose

Verify the ext2 backend's full functional surface through the VFS: mount, read, create, overwrite, unlink, nested directories, long file names, double-indirect large files, and readability of externally created images.

## Coverage

- Explicit `mount ext2 /mnt`; read the preloaded `hello.c`.
- Long-named file `verylongfilename.txt` (19 chars) read via the truncated alias rule.
- Create/overwrite/unlink a regular file; nested `mkdir d1` and `mkdir d1/d2`.
- `bigfile`: writes 1 MiB of a deterministic pattern and verifies it byte by byte (exercises direct + single-indirect + double-indirect paths), then unlinks (exercises the free path).
- The externally injected 1 MiB `hostbig.bin` (guaranteed to use double-indirect blocks) is fully read and checked with `wc`.

## Key assertions

- expect: `mount: ext2 on /mnt dev 2`; file content strings; `bigfile: ok`; `0 1 1048576`.
- reject: `panic:`, `kerneltrap`, `mount: failed`, `cannot stat`, `bigfile: FAIL`, `failed to delete`, and the open-failure strings.

## Links

- Contest task 1 (ext2 support); D1 (double indirect), D2 (directory coverage), D3 (long-name alias).
