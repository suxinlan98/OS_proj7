# error

[中文](README.md) | [English](README.en.md)

## 目的

验证 VFS 非法操作路径的稳定性：错误输入必须干净地失败，不得造成 panic 或系统状态损坏。

## 覆盖点

- 挂载到不存在的目录（`mount ext2 /no_such_dir`）。
- 拒绝通过 `/.`、`/..` 路径别名覆盖原生根文件系统。
- 在 CPUS=3 下分别向同一 ext2/FAT32 挂载点发起后台与前台请求；runner 只在后台命令后保留 10 ms 调度机会，前台命令结束后再等待提示符稳定，避免后续 UART 输入交错。`mtable.oplock` 串行化完整 mount 调用，每组只能有一个成功，用于验证最终互斥与失败语义；`MOUNTING` 发布、失败回滚与 unlink 可见性由锁序和源码审查保证，测试不宣称确定性命中该窗口。
- ext2 挂载成功后，拒绝同类型挂到另一目录（`mount ext2 /mnt2`），并拒绝 FAT32 叠加到已有 ext2 挂载路径（`mount fat32 /mnt`）。
- 正常挂载 ext2 与 fat32 后，尝试跨文件系统硬链接（`ln /mnt/hello.c /fat/cross.txt`）。
- 删除不存在的文件（ext2 与 fat32 各一）。

## 关键断言

- expect-count：`mount: failed` 恰好出现 7 次，分别对应无效目录、两个根目录别名、两组并发请求中的失败者、同类型不同目录和不同类型同一路径；ext2/FAT32 成功提示各恰好出现 1 次。
- expect：其他错误路径的 `failed` 提示；`ERROR_PATH_DONE` 到达结尾（证明系统仍在正常运行）。
- reject：`panic:`、`kerneltrap`。

## 关联

- 赛题“系统运行稳定”要求；B5（非法挂载拒绝）。
