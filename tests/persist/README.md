# persist

[中文](README.md) | [English](README.en.md)

## 目的

验证对 ext2/fat32 的写入真正落盘到 `fs.img`：写入后关闭并重启 QEMU，重新挂载并逐字节读回校验。区分“真实文件系统”与“内存演示”。

## 覆盖点

- 第一段会话（write.cmds.sh）：挂载双后端，写入 `persist.txt`/`p.txt`并即时读回；`bigfile /mnt/bigfile keep` 写入 1 MiB 双间接大文件。
- 重启 QEMU（镜像不变）。
- 第二段会话（read.cmds.sh）：重新挂载，读回全部文件；`bigfile verify` 逐字节校验 1 MiB 内容；`wc` 复核外部注入大文件。

## 关键断言

- expect：两会话的挂载成功串；`persist-ext2`/`persist-fat` 读回；`bigfile: ok (1048576 bytes, keep)` 与 `(..., verify)`；`0 1 1048576`；两个 DONE 标记。
- reject：`panic:`、`kerneltrap`、`mount: failed`、`bigfile: FAIL`、各 open 失败串。

## 关联

- 赛题“系统运行稳定”与持久化要求；D1（双间接）跨启动一致性。
