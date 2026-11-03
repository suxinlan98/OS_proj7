# mount_storm

[中文](README.md) | [English](README.en.md)

## 目的

在 CPUS=3 下同时制造路径 lookup/open 与 mount/umount 拓扑切换，验证短期 reader pin、长期 inode 引用 busy 检测和四态发布协议。

## 覆盖点

- 参数化 `umountstress`，分别对 ext2 和 FAT32 执行相同负载。
- 每个后端运行 2 个 lookup worker 和 2 个 topology worker，每个 worker 256 轮。
- 两个后端合计 2048 次并发 worker 迭代；开始时还确定性验证打开文件使卸载 busy。
- 压力结束后重新读取探针文件，验证最终拓扑和数据仍可用。

## 判定方法

必须分别出现 `UMOUNT_STRESS_OK ext2 256 loops` 和 `UMOUNT_STRESS_OK fat32 256 loops`；拒绝 panic、trap、挂载失败、worker 失败和最终读取失败。程序输出 ticks 供同环境复现，不设置脆弱的绝对耗时阈值。

## 运行

```sh
make mount_storm && make run
make loongarch_mount_storm && make run
```
