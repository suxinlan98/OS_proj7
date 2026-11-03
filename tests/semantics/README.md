# semantics

[中文](README.md) | [English](README.en.md)

## 目的

验证基础冒烟测试未覆盖的 VFS 语义：跨后端执行 ELF、挂载点 `..` 解析、硬链接与 unlink-while-open，以及安全卸载和重挂载。

## 覆盖点

- 将根文件系统中的 `/echo` ELF 分别复制到 ext2 与 fat32 后执行，验证 `exec()` 的 ELF 头、程序头和程序段读取均经过 VFS 分发。
- 挂载根目录的 `..`：`/mnt/../parent.txt` 与 `/fat/../parent.txt` 必须解析回 xv6 根文件系统。
- ext2 同文件系统硬链接成功且内容一致；fat32 硬链接显式拒绝。
- `unlinkopen /mnt/uop.txt` 与 `unlinkopen /fat/uop.txt`：打开中的文件被 unlink 后仍可读写，最后一次 close 才释放（B2 回归守护，双后端）。
- cwd 位于挂载文件系统时 `umount` 必须 busy；回到 `/` 后卸载成功、下层 xv6 目录重新可见，ext2/FAT32 随后可重挂载并再次读取数据或执行 ELF。

## 关键断言

- expect：`EXT2_EXEC_OK`、`FAT32_EXEC_OK` 均精确出现 2 次（命令回显 + 程序真实输出），`umount: busy or invalid` 精确 2 次，以及 `VFS_PARENT_OK`、底层目录标记、`FAT32_REMOUNT_OK`、`UMOUNT_SEMANTICS_OK` 与 `SEMANTICS_DONE`。
- reject：跨后端 `exec` 失败串、`panic:`、`kerneltrap`、`mount: failed`、`unlinkopen: FAIL`、ext2 链接失败串。

## 关联

- P0 `exec()` VFS 分发；B2（unlink 延迟释放）；B3 挂载点保护；安全卸载生命周期。
