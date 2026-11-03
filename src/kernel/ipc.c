// Synchronous IPC between clients (any process, via system calls) and the
// user-space service processes, plus the small set of privileged system
// calls a service needs: register, receive, reply, bulk transfer and raw
// block I/O.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#include "ipc.h"

struct svc {
  struct spinlock lock;
  struct proc *server;      // registered server, 0 = service down
  int gen;                  // incarnation; handles of older ones are stale
  int busy;                 // request slot taken by a client
  int has_req;              // request queued, not yet received by server
  int done;                 // reply available
  struct proc *client;
  struct ipcmsg *req;       // lives on the blocked client's kernel stack
  uint64 gaddr, glen;       // grant: the client buffer the server may touch
  int gkernel;              // grant address is a kernel address
  long ret, r1, r2;
} svc[NSVC];

void
ipcinit(void)
{
  int i;
  for(i = 1; i < NSVC; i++)
    initlock(&svc[i].lock, "svc");
}

int
svc_gen(int id)
{
  return svc[id].gen;
}

int
svc_alive(int id)
{
  return svc[id].server != 0;
}

int
svc_serverpid(int id)
{
  struct proc *s = svc[id].server;
  return s ? s->pid : 0;
}

// Send m to service id and wait for the reply.  The client's buffer
// [gaddr, gaddr+glen) (user address, or kernel address if gkernel) is
// made accessible to the server for the duration of the call.
// Returns m->ret, or -1 if the service is down, died, or the handle in
// m->gen belongs to a previous incarnation.
long
ipc_call(int id, struct ipcmsg *m, uint64 gaddr, uint64 glen, int gkernel)
{
  struct svc *s = &svc[id];
  struct proc *p = myproc();
  long ret;

  m->pid = p->pid;
  m->gaddr = gaddr;
  m->glen = glen;
  acquire(&s->lock);
  for(;;){
    if(s->server == 0 || (m->gen != 0 && m->gen != s->gen)){
      release(&s->lock);
      return -1;
    }
    if(!s->busy)
      break;
    sleep(&s->busy, &s->lock);
  }
  s->busy = 1;
  s->client = p;
  s->req = m;
  s->gaddr = gaddr;
  s->glen = glen;
  s->gkernel = gkernel;
  s->done = 0;
  s->has_req = 1;
  wakeup(&s->has_req);
  // Uninterruptible: the server may be writing into our memory.
  while(!s->done)
    sleep(p, &s->lock);
  ret = s->ret;
  m->r1 = s->r1;
  m->r2 = s->r2;
  s->busy = 0;
  s->client = 0;
  s->req = 0;
  wakeup(&s->busy);
  release(&s->lock);
  return ret;
}

// Called when process p exits: if it was a server, fail the request in
// flight and everybody queued behind it; clients see -1 until a new
// server registers.
void
ipc_proc_exit(struct proc *p)
{
  int i;
  for(i = 1; i < NSVC; i++){
    struct svc *s = &svc[i];
    acquire(&s->lock);
    if(s->server == p){
      s->server = 0;
      if(s->busy && !s->done){
        s->ret = -1;
        s->r1 = s->r2 = 0;
        s->done = 1;
        s->has_req = 0;
        wakeup(s->client);
      }
      wakeup(&s->busy);
    }
    release(&s->lock);
  }
  p->xfer_client = 0;
}

// svc_register(id): become the server of service id.
uint64
sys_svc_register(void)
{
  int id;
  struct svc *s;

  argint(0, &id);
  if(id <= 0 || id >= NSVC)
    return -1;
  s = &svc[id];
  // only the process the kernel started from the boot module may serve
  if(myproc()->svcid != id)
    return -1;
  acquire(&s->lock);
  if(s->server != 0){
    release(&s->lock);
    return -1;
  }
  s->server = myproc();
  s->gen++;
  release(&s->lock);
  return s->gen;
}

// svc_recv(id, struct ipcmsg *): wait for the next request.
uint64
sys_svc_recv(void)
{
  int id;
  uint64 uaddr;
  struct svc *s;
  struct proc *p = myproc();
  struct ipcmsg m;

  argint(0, &id);
  argaddr(1, &uaddr);
  if(id <= 0 || id >= NSVC)
    return -1;
  s = &svc[id];
  acquire(&s->lock);
  if(s->server != p){
    release(&s->lock);
    return -1;
  }
  while(!s->has_req){
    if(killed(p)){
      release(&s->lock);
      return -1;
    }
    sleep(&s->has_req, &s->lock);
  }
  m = *s->req;
  s->has_req = 0;
  p->xfer_client = s->client;
  release(&s->lock);
  if(copyout(p->pagetable, uaddr, (char *)&m, sizeof(m)) < 0)
    return -1;
  return 0;
}

// svc_reply(id, ret, r1, r2)
uint64
sys_svc_reply(void)
{
  int id;
  uint64 ret, r1, r2;
  struct svc *s;
  struct proc *p = myproc();

  argint(0, &id);
  argaddr(1, &ret);
  argaddr(2, &r1);
  argaddr(3, &r2);
  if(id <= 0 || id >= NSVC)
    return -1;
  s = &svc[id];
  acquire(&s->lock);
  if(s->server != p || !s->busy || s->done || s->has_req){
    release(&s->lock);
    return -1;
  }
  s->ret = (long)ret;
  s->r1 = (long)r1;
  s->r2 = (long)r2;
  s->glen = 0;
  s->done = 1;
  p->xfer_client = 0;
  wakeup(s->client);
  release(&s->lock);
  return 0;
}

// svc_xfer(id, dir, caddr, buf, n): copy between the server's buf and
// the granted client buffer at caddr.
uint64
sys_svc_xfer(void)
{
  int id, dir, n;
  uint64 caddr, buf;
  struct svc *s;
  struct proc *p = myproc(), *c;
  char tmp[256];
  int gkernel;
  uint64 gaddr, glen;

  argint(0, &id);
  argint(1, &dir);
  argaddr(2, &caddr);
  argaddr(3, &buf);
  argint(4, &n);
  if(id <= 0 || id >= NSVC || n < 0)
    return -1;
  s = &svc[id];
  acquire(&s->lock);
  if(s->server != p || !s->busy || s->done || s->has_req || s->glen == 0){
    release(&s->lock);
    return -1;
  }
  c = s->client;
  gaddr = s->gaddr;
  glen = s->glen;
  gkernel = s->gkernel;
  release(&s->lock);

  if(caddr < gaddr || caddr + n < caddr || caddr + n > gaddr + glen)
    return -1;
  while(n > 0){
    int m = n > (int)sizeof(tmp) ? (int)sizeof(tmp) : n;
    if(dir == XFER_FROM_CLIENT){
      if(gkernel)
        memmove(tmp, (void *)caddr, m);
      else if(copyin(c->pagetable, tmp, caddr, m) < 0)
        return -1;
      if(copyout(p->pagetable, buf, tmp, m) < 0)
        return -1;
    } else {
      if(copyin(p->pagetable, tmp, buf, m) < 0)
        return -1;
      if(gkernel)
        memmove((void *)caddr, tmp, m);
      else if(copyout(c->pagetable, caddr, tmp, m) < 0)
        return -1;
    }
    caddr += m;
    buf += m;
    n -= m;
  }
  return 0;
}

// blk_rw(dev, blockno, buf, write): raw block I/O for the file system
// server; the only disk access path left in the kernel (driver + HAL).
uint64
sys_blk_rw(void)
{
  int dev, blockno, write;
  uint64 uaddr;
  struct buf *b;
  struct proc *p = myproc();

  argint(0, &dev);
  argint(1, &blockno);
  argaddr(2, &uaddr);
  argint(3, &write);
  if(svc[SVC_FS].server != p)
    return -1;
  if((b = kalloc()) == 0)
    return -1;
  memset(b, 0, sizeof(*b));
  initsleeplock(&b->lock, "blk");
  b->dev = dev;
  b->blockno = blockno;
  if(write){
    if(copyin(p->pagetable, (char *)b->data, uaddr, BSIZE) < 0){
      kfree(b);
      return -1;
    }
    hal_disk_rw(b, 1);
  } else {
    hal_disk_rw(b, 0);
    if(copyout(p->pagetable, uaddr, (char *)b->data, BSIZE) < 0){
      kfree(b);
      return -1;
    }
  }
  kfree(b);
  return 0;
}

// svc_pid(id): pid of the live server, 0 if the service is down.
uint64
sys_svc_pid(void)
{
  int id;
  argint(0, &id);
  if(id <= 0 || id >= NSVC)
    return -1;
  return svc_serverpid(id);
}

// svc_restart(id): re-spawn a dead service from its boot module.
uint64
sys_svc_restart(void)
{
  int id;
  argint(0, &id);
  if(id != SVC_FS || svc[id].server != 0)
    return -1;
  return spawn_fsserver();
}

// ---- file system client helpers (used by the system call layer) ----

// Call the file system server on behalf of the current process.
long
fs_call(struct ipcmsg *m, uint64 gaddr, uint64 glen, int gkernel)
{
  struct proc *p = myproc();
  m->cwd = (p->cwdh && p->cwdgen == svc_gen(SVC_FS)) ? p->cwdh : 0;
  return ipc_call(SVC_FS, m, gaddr, glen, gkernel);
}

// Drop a server handle; stale handles (dead server) need no message.
void
fs_release(int h, int gen)
{
  struct ipcmsg m;

  if(h <= 0 || gen != svc_gen(SVC_FS) || !svc_alive(SVC_FS))
    return;
  memset(&m, 0, sizeof(m));
  m.op = FS_CLOSE;
  m.h = h;
  m.gen = gen;
  ipc_call(SVC_FS, &m, 0, 0, 0);
}

// Give a new process its own reference to p's cwd.
void
fs_dup_cwd(struct proc *np, struct proc *p)
{
  struct ipcmsg m;

  np->cwdh = 0;
  np->cwdgen = 0;
  if(p->cwdh <= 0 || p->cwdgen != svc_gen(SVC_FS))
    return;
  memset(&m, 0, sizeof(m));
  m.op = FS_DUP;
  m.h = p->cwdh;
  m.gen = p->cwdgen;
  long h = ipc_call(SVC_FS, &m, 0, 0, 0);
  if(h > 0){
    np->cwdh = h;
    np->cwdgen = p->cwdgen;
  }
}
