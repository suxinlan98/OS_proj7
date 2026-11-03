# fault — 用户态文件系统服务的故障隔离与恢复

微内核化后，文件系统是一个普通用户进程（`fssrv`），内核只做 IPC、调度、内存与块设备驱动。本测试点验证它的故障**可隔离、可恢复**。

驱动程序 `src/user/fsfault.c` 在 xv6 shell 中运行，依次做：

| 步骤 | 注入方式 | 断言 |
|---|---|---|
| 1 | `kill(svc_pid(FS))` 杀掉服务进程 | 管道/fork 等不依赖文件系统的功能照常；init 拉起新的服务进程（新 pid） |
| 2 | 崩溃前持有的文件描述符、cwd | 旧句柄被拒绝（返回 -1，不挂死、不误用新服务的数据）；`close` 无副作用；崩溃前已提交的数据完好 |
| 3 | `mount("__crash__", …)` 让服务内部空指针解引用 | 与 kill 同样可恢复 |
| 4 | 子进程高强度创建/写/删文件，同时连杀服务 6 次 | 系统不 panic、客户端不挂死；旧数据完好；日志重放（`recovering tail …`）可见；文件系统最终可用 |
| 5 | 已挂载 ext2 时杀服务 | 挂载表属于服务状态，重启后丢失；重新 `mount ext2` 后数据可读，`umount` 正常 |

通过标准：输出 `FSFAULT_DONE` 与全部 `fsfault: …: OK`，且没有 `: FAIL`、`panic:`、`kerneltrap`。

运行：`make fault && make run`（RISC-V）；`make loongarch_fault && make run`（LoongArch）。
