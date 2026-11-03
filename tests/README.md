# tests

[中文](README.md) | [English](README.en.md)

本目录是项目的测试编排体系（宿主机侧）：测试用例、驱动脚本、运行入口与通过证据。镜像内的 `/tests` 内容（官方测例数据）位于 `src/tests/`，二者职责不同，见根 `README.md`。

## 布局

```text
tests/
|-- README.md / README.en.md     本文件（双语）
|-- MATRIX.md                    测试覆盖矩阵
|-- VFS_TESTS_PIN.txt            官方测例版本锚定
|-- check_env.sh                 环境自检
|-- run_all.sh                   22 项一键双架构回归
|-- full_usertests.sh            完整 usertests（不带 -q）
|-- runner/
|   `-- run_qemu_case.py         QEMU 测试驱动
|-- evidence/                    通过证据归档（README + 原始输出）
|-- test_baseline/               官方测例 A：usertests（快速版/完整版）
|-- public_vfs/                  官方测例 B：公开 VFS 流程复刻
|-- ext2/                        ext2 功能面（含双间接大文件、长名）
|-- fat32/                       fat32 功能面（含 LFN 长名）
|-- error/                       错误路径稳定性
|-- semantics/                   挂载/链接/unlink 语义
|-- persist/                     双会话持久性
|-- smp/                         CPUS=3 文件 I/O 并发压测
|-- mount_lifecycle/             挂载状态机、嵌套拓扑与失败回滚
|-- remount_io/                  双后端 1 MiB 多轮重挂载校验
`-- mount_storm/                 CPUS=3 双后端挂卸载风暴
```

每个测试点目录为统一四件套：`README.md`、`*.cmds.sh`（喂给 xv6 shell 的命令）、`run.sh`（expect/reject 断言）、`logs/`（运行产物，不入库）。

## 测试点

- `test_baseline`：官方自带测例（赛题任务 3），`usertests -q`；完整版由 `full_usertests.sh` 承担。
- `public_vfs`：官方 VFS 评测流程逐字节复刻（赛题任务 4）。
- `ext2`：挂载/读写/删除/嵌套目录/长名别名/1MiB 双间接大文件/外部大文件。
- `fat32`：挂载/8.3 增删改/嵌套目录/LFN 长名三条访问路径。
- `error`：非法挂载、跨盘硬链接、删除不存在文件等错误路径不 panic。
- `semantics`：挂载点 `..`、ext2 硬链接、fat32 拒链、unlinkopen，以及 umount busy/成功/底层目录恢复/重挂载。
- `persist`：写入→重启→重挂载→逐字节读回（含 1MiB 双间接文件）。
- `smp`：CPUS=3 三路并发 bigfile + 挂载点内 stressfs + lookup/mount/umount 竞态（C1 与卸载生命周期回归）。
- `mount_lifecycle`：非法卸载、后端失败回滚、挂载点删除保护、嵌套挂载 busy、下层目录恢复和跨挂载点重挂载。
- `remount_io`：ext2/FAT32 各写入 1 MiB，三轮卸载/重挂载后逐字节复验，并检查每轮下层目录恢复。
- `mount_storm`：CPUS=3 下，ext2/FAT32 各由 2 个 lookup worker 与 2 个拓扑 worker 执行 256 轮竞争。

## 运行

仓库根目录两步式：

```sh
make <name> && make run        # name: 测试点名（如 ext2、smp）
make loongarch_<name> && make run
```

聚合入口：

```sh
bash tests/run_all.sh          # 11 测试点 × 2 架构 = 22 项回归
bash tests/full_usertests.sh riscv
bash tests/full_usertests.sh loongarch
make verify-all                # 环境检查 + 全部上述内容
```

## 推送前门禁

推送远端前必须全部通过：

```sh
bash tests/run_all.sh                      # 22/22 双架构
bash tests/full_usertests.sh riscv         # 完整 usertests（无 -q）
bash tests/full_usertests.sh loongarch
```

## 规范约定

1. 命令文件：单会话测试点用 `cmds.sh`；多会话用 `<阶段>.cmds.sh`（如 `write.cmds.sh`/`read.cmds.sh`）。
2. 断言模式：`--expect` 只写“成功才出现的串”；`--expect-count` 验证次数和状态转换；`--reject` 只写“失败/异常串”；每个测试点必须含 `panic:`/`kerneltrap` 负断言。
3. 使用 `--prompt-wait` 的用例会在首条和后续命令前等待 shell 提示符稳定，命令超时直接失败，避免串口丢字符被误判为内核缺陷。
4. `logs/` 为本地运行产物，不入库；最终通过证据归 `evidence/`。
5. 新增测试点 = 新建同名目录（四件套；`test_baseline` 无 cmds.sh，`run.sh` 直接调官方入口 `test-xv6.py`，为唯一例外）+ 根 Makefile 目标 + `run_all.sh` 登记 + `MATRIX.md` 增行。
