# public_vfs

[中文](README.md) | [English](README.en.md)

## 目的

逐字节复刻赛题指定的官方 VFS 评测流程：从 xv6 文件系统启动，手动挂载 ext2 到 `/mnt`，然后 `cd tests; cat cmds.sh | sh` 运行官方 `test1`/`test2` 跨文件系统复制用例。对应赛题任务 4。官方测例文件的版本锚定见`tests/VFS_TESTS_PIN.txt`。

## 覆盖点

- 显式挂载 `mount ext2 /mnt`（不依赖自动挂载）。
- 镜像 `/tests` 目录布局与官方参考一致（cmds.sh、hello.c、test1、test2、hello）。
- test1：ext2↔xv6 四方向普通文件复制。
- test2：xv6→ext2 复制并读回验证内容。
- exec 裸命令名根目录回退（`/tests` 下直接运行 `cat`/`test1` 等根目录程序）。

## 关键断言

- expect：`mount: ext2 on /mnt dev 2`；四条 `test_copy_file(...): succeed`；`test_cross_fs_copy(): succeed`；`PUBLIC_VFS_DONE`。
- reject：`panic:`、`kerneltrap`、`mount: failed`、`exec cat failed`、`exec test1 failed`、`exec test2 failed`、`: error`。

## 关联

- 赛题任务 4。
- 官方测例来源与版本：`tests/VFS_TESTS_PIN.txt`。
