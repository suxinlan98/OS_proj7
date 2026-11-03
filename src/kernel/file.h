#include "types.h"

// An open file as seen by the kernel.  Everything that lives on a file
// system is owned by the user-space file system server; FD_REMOTE and
// FD_DEVICE files only remember the server's handle for it (and the
// server incarnation that issued it).
struct file {
  enum { FD_NONE, FD_PIPE, FD_REMOTE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  int h;             // FD_REMOTE, FD_DEVICE: server handle (0 = none)
  int gen;           // server incarnation that issued h
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
