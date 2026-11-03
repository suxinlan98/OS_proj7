# Boot module: the file system server ELF is linked into the kernel image,
# because the kernel has to start it before any file system is reachable
# (and again whenever the server has crashed).
        .section .rodata
        .global fssrv_start
        .global fssrv_end
        .align 4
fssrv_start:
        .incbin "user/_fssrv"
fssrv_end:
