# fat32

[中文](README.md) | [English](README.en.md)

## 目的

验证 fat32 后端经 VFS 的功能面：挂载、8.3 短名增删改、空文件、嵌套目录，以及 FAT32 长文件名（LFN）的读取与目录列举。

## 覆盖点

- 显式挂载 `mount fat32 /fat`。
- 8.3 短名：创建/覆盖/删除普通文件，空文件创建，`mkdir d1/d2` 嵌套目录。
- LFN 读取（夹具文件由 mkfat32 注入镜像）：`cat /fat/longfilename.txt`（16 字符全名，经 namei 截断后别名命中）；`cat /fat/shortlong.txt`（小写查询命中混合大小写 LFN，大小写不敏感）；`cat /fat/longfi~1.txt`（8.3 残名兼容路径）。
- `ls /fat` 显示装配长名（`longfilename.t` 截断、`ShortLong.txt` 原样），不显示 8.3 残名。

## 关键断言

- expect：`mount: fat32 on /fat dev 3`；内容串；`fat32-lfn-ok`；`longfilename.t`；`ShortLong.txt`。
- reject：`panic:`、`kerneltrap`、`mount: failed`、`LONGFI~1`（残名不得出现在 ls）、各 open/mkdir 失败串。

## 关联

- 赛题任务 1（fat32 支持）；D4（LFN 读支持）。
