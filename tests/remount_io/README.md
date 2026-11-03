# remount_io

[中文](README.md) | [English](README.en.md)

## 目的

验证 ext2/FAT32 大文件跨多轮卸载与重新挂载后的内容完整性，并观察下层目录在每次卸载后是否恢复。

## 覆盖点

- 两个后端各写入一个 1 MiB 确定性字节模式文件并立即校验。
- 完成三轮卸载/重挂载，每轮对两个文件逐字节复验。
- ext2 覆盖直接块、单间接块与双重间接块；FAT32 覆盖长簇链分配和遍历。
- 每轮卸载后读取 xv6 下层标记，验证挂载覆盖关系正确撤销。

## 判定方法

精确要求 8 次挂载、2 次 `keep` 成功、6 次 `verify` 成功和 6 次下层读取，总计校验 8 MiB。该用例证明正常卸载/重挂载完整性，不宣称模拟突然断电。

## 运行

```sh
make remount_io && make run
make loongarch_remount_io && make run
```
