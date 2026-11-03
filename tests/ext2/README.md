# ext2

[中文](README.md) | [English](README.en.md)

## 目的

验证 ext2 后端经 VFS 的完整功能面：挂载、读、创建、覆盖写、删除、嵌套目录、长文件名、双重间接块大文件、外部镜像大文件可读性。

## 覆盖点

- 显式挂载 `mount ext2 /mnt`，读取镜像预置的 `hello.c`。
- 长名文件 `verylongfilename.txt`（19 字符）经截断别名正常读取。
- 创建/覆盖/删除普通文件；`mkdir d1` 与 `mkdir d1/d2` 嵌套目录。
- `bigfile`：写入 1 MiB 确定性模式并逐字节读回校验（覆盖 12 直接 +单间接 + 双重间接全路径），随后 unlink（覆盖释放路径）。
- 镜像常驻注入的外部 1 MiB 文件 `hostbig.bin`（必含双重间接块），以 `wc` 校验全量字节可读。

## 关键断言

- expect：`mount: ext2 on /mnt dev 2`；文件内容串；`bigfile: ok`；`0 1 1048576`。
- reject：`panic:`、`kerneltrap`、`mount: failed`、`cannot stat`、`bigfile: FAIL`、`failed to delete` 及各 open 失败串。

## 关联

- 赛题任务 1（ext2 支持）；D1（双重间接）、D2（目录覆盖）、D3（长名别名）。
