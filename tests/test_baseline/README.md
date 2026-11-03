# test_baseline

[中文](README.md) | [English](README.en.md)

## 目的

在改造后的系统上运行 xv6-riscv rev5 自带的官方回归测试，确认原有进程、内存、文件系统与系统调用行为不退化。对应赛题任务 3（通过自带测例）。

## 覆盖点

- `usertests -q` 全部快速测试项（进程/内存/文件系统/系统调用等 60+ 项）。
- 双架构执行：RISC-V 与 LoongArch64，CPUS=3（经官方入口 `test-xv6.py`）。
- 完整版（不带 `-q`，另含 bigdir/manywrites/badwrite/execout/diskfull/outofinodes 六项慢测试）由 `tests/full_usertests.sh` 承担。

## 关键断言

- expect：输出以 `ALL TESTS PASSED` 结尾。
- reject：`panic:`。
- `run.sh` 内含 420 秒超时兜底：guest panic 时官方脚本会阻塞读死等，超时会将其转为可见的 FAIL 而非挂死。

## 关联

- 赛题任务 3。
- 完整版结果证据：`tests/evidence/usertests-full-*.txt`。
