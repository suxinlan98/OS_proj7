# mount_lifecycle

[中文](README.md) | [English](README.en.md)

## 目的

以确定性命令序列验证 `mount/umount` 的状态机、失败回滚、挂载点保护与嵌套拓扑语义。

## 覆盖点

- 拒绝不存在路径、根目录、`/.` 和 `/..` 的卸载。
- 用错误设备分别触发 ext2/FAT32 后端初始化失败，随后正确挂载，验证表项已回滚。
- 挂载中的目录不能 `rm`；子挂载存在时父挂载必须 busy。
- 卸载子挂载后恢复 ext2 下层目录，卸载父挂载后恢复 xv6 下层目录。
- ext2/FAT32 重挂载后数据仍可访问，且 FAT32 可从嵌套路径迁移到 `/fat` 挂载。

## 判定方法

runner 使用 `--expect-count` 精确验证 5 次非法/忙卸载、2 次挂载失败、2 次挂载点删除拒绝，以及四类可见性标记的出现次数；同时拒绝 `panic:`、`kerneltrap` 和读取失败。

## 运行

```sh
make mount_lifecycle && make run
make loongarch_mount_lifecycle && make run
```
