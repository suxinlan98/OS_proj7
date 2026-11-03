# proj7 — AI 辅助的 xv6 微内核化重构

基于 proj6（xv6-riscv rev5 + VFS + HAL，RISC-V / LoongArch，xv6 / ext2 / fat32），把文件系统整体移到用户态服务进程 `fssrv`，内核与客户端通过同步 IPC 与其通信。

* 设计：`design.md`　AI 提示词记录：`prompts.md`
* 目录：`src/kernel`（微内核）、`src/fssrv`（文件系统服务）、`src/user`（用户程序，含 `fsfault` 故障测例）、`tests/`（宿主机测试，沿用 proj6 的 11 项并新增 `fault`）
* 构建/测试：`make <name> && make run`，`make loongarch_<name> && make run`；全量：`bash tests/run_all.sh`（24 项：12 × 2 架构，其中 `fault` 为新增）
* 工具链：`riscv64-linux-gnu-gcc`、`loongarch64-linux-gnu-gcc`、QEMU ≥ 7.2
