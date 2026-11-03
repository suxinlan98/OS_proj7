# 测试覆盖矩阵

11 个测试点 × 双架构的覆盖总览。统一入口与断言机制见 `tests/README.md`，本地原始日志位于各测试点的 `logs/`。

| 测试点 | 主要负载与方法 | 关键质量属性 | RISC-V | LoongArch |
|---|---|---|---|---|
| test_baseline | rev5 `usertests -q` | 赛题任务 3、原版语义兼容 | PASS | PASS |
| public_vfs | 官方流程、`/tests` 布局、四向跨盘复制 | 赛题任务 4、接口兼容 | PASS | PASS |
| ext2 | 增删改查、嵌套目录、长名、1 MiB 双间接、外部大文件 | ext2 功能完整性 | PASS | PASS |
| fat32 | 8.3 增删改查、空文件、嵌套目录、LFN 三路径 | FAT32 功能完整性 | PASS | PASS |
| error | 非法挂载、同类互斥、跨盘硬链接、缺失文件 | 错误隔离、失败后可继续运行 | PASS | PASS |
| semantics | 跨后端 ELF、挂载点 `..`、硬链接、unlink-open、基础 umount | VFS 语义一致性 | PASS | PASS |
| persist | 写入→关闭 QEMU→重启→读回，含 1 MiB 文件 | 跨启动持久性 | PASS | PASS |
| smp | CPUS=3 三路大文件、stressfs、基础挂卸载竞态 | 后端分配与 I/O 并发 | PASS | PASS |
| mount_lifecycle | 失败回滚、挂载点保护、嵌套 busy、下层恢复、重挂载 | 四态生命周期与拓扑语义 | PASS | PASS |
| remount_io | 双后端 1 MiB 写入；三轮重挂载、6 次逐字节复验 | 大文件、缓存生命周期、后端重初始化 | PASS | PASS |
| mount_storm | CPUS=3；双后端各 4 worker × 256 轮 | lookup/open 与 mount/umount 竞争 | PASS | PASS |

补充验证（不计入 22 项）：

- 完整版 `usertests`：在快速版之外增加 6 项慢测试和内存泄漏检查，双架构均输出 `ALL TESTS PASSED`。
- `e2fsck -fn`：对 ext2 镜像执行五阶段离线只读检查，归档证据为零错误。
- `vfsbench`：用于最终源码性能复测；历史误标的 LoongArch 数据不得引用。

测试结论的边界：`remount_io` 证明同会话内正常卸载/重挂载后的数据完整性（跨 QEMU 会话的持久性由 `persist` 承担）；它不等价于断电崩溃一致性。`mount_storm` 记录 ticks 便于复现，但不设置跨宿主机的绝对性能阈值。
