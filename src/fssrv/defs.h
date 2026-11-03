// Declarations for the file system server (the user-space counterpart of
// the old kernel defs.h, limited to the code that moved here).
struct buf;
struct inode;
struct mount;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

// block.c
int             block_read_bytes(uint, uint, uchar*, uint);
int             block_write_bytes(uint, uint, uchar*, uint);

// main.c: disk access goes to the kernel's block driver
void            hal_disk_rw(struct buf*, int);

// fs.c
void            fsinit(int);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   iget(uint, uint);
struct inode*   idup(struct inode*);
void            iinit();
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, int, uint64, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, int, uint64, uint, uint);
void            itrunc(struct inode*);
void            ireclaim(int);
int             vfs_mount_busy(struct mount*);
extern struct inode *fs_cwd;

// vfs.c
void            vfsinit(void);
void            vfs_init_inode(struct inode*);
int             vfs_mount(char*, char*, int);
int             vfs_umount(char*);
struct mount*   vfs_mountpoint(struct inode*);
struct inode*   vfs_mounted_root(struct inode*);
struct inode*   vfs_lookup(struct inode*, char*);
struct inode*   vfs_create(struct inode*, char*, short, short, short);
int             vfs_unlink(struct inode*, char*);
int             vfs_link(struct inode*, struct inode*, char*);
int             vfs_readi(struct inode*, int, uint64, uint, uint);
int             vfs_writei(struct inode*, int, uint64, uint, uint);
void            vfs_stati(struct inode*, struct stat*);
void            vfs_itrunc(struct inode*);

// ext2.c / fat32.c
struct fs_ops*  ext2_get_fs_ops(void);
struct fs_ops*  fat32_get_fs_ops(void);

// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(void);
void            end_op(void);

// main.c: environment shims
int             printf(char*, ...) __attribute__ ((format (printf, 1, 2)));
void            panic(char*) __attribute__((noreturn));
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);

// Locks vanish: one request at a time.
#define initlock(l, n)      ((void)0)
#define acquire(l)          ((void)0)
#define release(l)          ((void)0)
#define initsleeplock(l, n) ((void)0)
#define acquiresleep(l)     ((void)0)
#define releasesleep(l)     ((void)0)
#define holdingsleep(l)     1
#define wakeup(c)           ((void)0)
// Nothing can run concurrently, so waiting could never end.
#define sleep(c, l)         panic("fssrv: would block")

// string.c (compat.c)
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
