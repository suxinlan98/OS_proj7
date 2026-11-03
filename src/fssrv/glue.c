// Glue between the file system code (which used to be part of the kernel)
// and its new environment: a user process that talks to the kernel only
// through system calls.
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/fs.h"
#include "kernel/ipc.h"
#include "user/user.h"
#include "fssrv/spinlock.h"
#include "fssrv/sleeplock.h"
#include "fssrv/buf.h"

void
panic(char *s)
{
  printf("fssrv: panic: %s\n", s);
  // Dying is the fault-containment strategy: the kernel fails the pending
  // request, init starts a fresh server and log recovery restores the disk.
  exit(1);
}

// Move data between a client buffer (user_* != 0, address in the client's
// space, within the buffer granted for the current request) and the server.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  if(user_dst)
    return svc_xfer(SVC_FS, XFER_TO_CLIENT, dst, src, len);
  memmove((void *)dst, src, len);
  return 0;
}

int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  if(user_src)
    return svc_xfer(SVC_FS, XFER_FROM_CLIENT, src, dst, len);
  memmove(dst, (void *)src, len);
  return 0;
}

void
hal_disk_rw(struct buf *b, int write)
{
  if(blk_rw(b->dev, b->blockno, b->data, write) < 0)
    panic("disk i/o");
  b->valid = 1;
}

int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

char*
strncpy(char *s, const char *t, int n)
{
  char *os = s;

  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  while(n-- > 0)
    *s++ = 0;
  return os;
}

char*
safestrcpy(char *s, const char *t, int n)
{
  char *os = s;

  if(n <= 0)
    return os;
  while(--n > 0 && (*s++ = *t++) != 0)
    ;
  *s = 0;
  return os;
}
