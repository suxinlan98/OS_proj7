#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define EXT2DEV       2  // logical device number of ext2 disk image
#define EXT2SIZE   4096  // ext2 image size in xv6 blocks
#define FAT32DEV      3  // logical device number of fat32 disk image
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGBLOCKS    (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       2000  // size of file system in blocks
#define FAT32START   (FSSIZE + EXT2SIZE)  // fat32 image start in xv6 blocks
#define MAXPATH      128   // maximum file path name
#define USERSTACK    1     // user stack pages
