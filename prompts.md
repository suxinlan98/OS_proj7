# 关键 AI 提示词记录

本项目使用 Claude Code（claude-sonnet-5-5）完成。按时间顺序记录推动设计与实现的关键提示词（原文）及其产出，便于复核 AI 辅助过程。

## P1 任务下达（用户）

> 完成 https://os.xtnl.org.cn/#/sList?TYPE=26OS_ZXHNF 的 proj7 AI辅助的xv6微内核化重构
>
> 赛题背景：xv6-riscv 是 MIT 开发的宏内核教学操作系统，本项目的任务是利用 AI 编程工具来重构 xv6-riscv 的 2026 北航扩展版（已支持 VFS 和 HAL），将其变为微内核架构。要求支持原来的所有测例，且增加的代码尽量少、系统运行稳定。
> 赛题任务：微内核化重构 xv6-riscv，文件系统、设备驱动等非核心功能应以用户态服务进程存在，与服务进程的通信应以 IPC 方式进行；通过 xv6-riscv 第5版自带的测例和 2026 北航扩展版的测例；编写测例验证非核心服务故障可隔离、可恢复；保持同时支持 RISC-V 和 LoongArch；保持同时支持 xv6、ext2 和 fat32 文件系统；记录提交所有关键的 AI 提示词。

产出：确定以 `proj6`（已有 VFS+HAL 的 xv6）为基础；读代码后给出方案——文件系统整体作为用户态服务，内核保留进程/IPC/块驱动。

## P2 方案推导（AI 内部任务分解，来自对 P1 的规划）

1. 内核代码盘点：`fs.c log.c bio.c block.c vfs.c ext2.c fat32.c` 与 `sysfile.c` 是文件系统，其余是内核。
2. 最小改动原则：原文件系统源码**原样搬迁**到用户进程，用垫片头文件（空锁、`either_copy*`→授权拷贝、`hal_disk_rw`→`blk_rw`）屏蔽差异，避免重写 4 900 行代码。
3. IPC 设计：同步单槽请求、消息只带小参数、大块数据走“授权拷贝”，句柄 + 代数（generation）处理服务重启后的陈旧引用。
4. 自举问题：文件系统服务要在文件系统可用前启动，因此以 boot module（`.incbin`）嵌入内核；`init` 监督并通过 `svc_restart` 重启。
5. 测试：先跑通 RISC-V 的 `usertests -q`，再跑 proj6 的 11 项测例双架构，最后新增 `fault` 测例。

## P3 调试类提示（节选）

* “panic: acquire” 的定位提示：`allocproc` 返回时持有 `p->lock`，`userinit` 中第二次 `allocproc` 会重复获取 → 先释放再分配。
* “fs_dup_cwd 在持有 np->lock 时睡眠” → 把对服务的 IPC 移到 `release(&np->lock)` 之后（`public_vfs`/`semantics` 因此失败过一次，修复后通过）。
* “服务代客户端缺页”问题：`vmfault` 使用 `myproc()` 的 `sz`，服务进程填充客户端惰性分配页时需切换到客户端 → 新增 `proc.xfer_client`。
* “服务重启时 `svc_register` 不能重置 `busy/done`”：避免正在被唤醒的旧客户端永久睡眠。

## P4 故障测例设计

> 编写测例验证非核心服务故障可隔离、可恢复

→ `src/user/fsfault.c`：杀服务、服务内部空指针崩溃、旧句柄/旧 cwd 拒绝、并发负载下连续 6 次杀服务（观察日志重放）、已挂载 ext2 时重启后重新挂载。接入 `make fault` / `make loongarch_fault` 与 `tests/run_all.sh`。
