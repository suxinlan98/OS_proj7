# evidence

[中文](README.md) | [English](README.en.md)

本目录归档关键测试的最终通过证据（原始 QEMU 输出、fsck 输出与汇总），供评审复核。`make verify-all` 可重新执行双架构功能回归和完整 usertests，但不会自动覆盖本目录；fsck 与性能数据使用下方独立命令复核。

## 内容

- `run_all.txt` — 全量回归汇总（11 个测试点 × 2 个架构 = 22 项执行）
- `usertests-full-riscv.txt` — RISC-V 完整 usertests 原始输出（不带 -q，含 6 项慢测试）
- `usertests-full-loongarch.txt` — LoongArch64 完整 usertests 原始输出
- `smp-riscv.txt` / `smp-loongarch.txt` — CPUS=3 并发压测原始输出（ext2/fat32 三路并发大文件写入 + 校验 + stressfs + lookup/mount/umount 竞态）
- `error-riscv.txt` / `error-loongarch.txt` — 本轮挂载冲突与错误路径原始 QEMU 输出
- `semantics-riscv.txt` / `semantics-loongarch.txt` — 本轮跨后端 ELF 执行、挂载点 `..`、链接、unlink-open、安全卸载与重挂载原始 QEMU 输出
- `mount-lifecycle-riscv.txt` / `mount-lifecycle-loongarch.txt` — 失败回滚、嵌套挂载、挂载点保护与下层恢复
- `remount-io-riscv.txt` / `remount-io-loongarch.txt` — 双后端 1 MiB 多轮重挂载与逐字节复验
- `mount-storm-riscv.txt` / `mount-storm-loongarch.txt` — CPUS=3 双后端挂卸载风暴原始输出
- `e2fsck-final.txt` — 最终 ext2 测试镜像的 `e2fsck -fn` 五阶段只读检查
- `bench-riscv.txt` — 本轮 P0 修复前采集的 RISC-V vfsbench 三后端历史基准，不代表最终源码性能
- `bench-loongarch.txt` — 文件名保留用于审计，但内容实际为误采集的 RISC-V 记录，不得作为 LoongArch 性能证据；需重新采集
- `bench-rev5-baseline.txt` — 与上述历史记录配套的原版 rev5 同负载对照
- `bench-fat32-before-opts.txt` / `bench-fat32-nextfree-only.txt` — 本轮 P0 修复前的 fat32 分配/漫游优化前与中间态对照

## 功能回归复现

```sh
make verify-all
```

或分步：

```sh
bash tests/check_env.sh
bash tests/run_all.sh
bash tests/full_usertests.sh riscv
bash tests/full_usertests.sh loongarch
make smp && make run
make loongarch_smp && make run
```

上述单项运行的原始 QEMU 日志写入 `tests/<case>/logs/`。确认结果后再归档到本目录，避免把失败或中断的运行覆盖为最终证据。

ext2 镜像只读一致性检查：

```sh
e2fsck -fn src/ext2.img
```

性能基准的负载、命令和当前证据边界见 `docs/contest/performance.md`；`make verify-all` 不执行 `vfsbench`。
