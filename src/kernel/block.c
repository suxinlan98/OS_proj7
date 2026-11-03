// SPDX-License-Identifier: BSD-2-Clause
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

static uint
block_min(uint a, uint b)
{
  return a < b ? a : b;
}

int
block_read_bytes(uint dev, uint off, uchar *dst, uint n)
{
  struct buf *bp;
  uint m, tot;

  if(dst == 0 && n != 0)
    return -1;

  for(tot = 0; tot < n; tot += m, off += m){
    bp = bread(dev, off / BSIZE);
    m = block_min(n - tot, BSIZE - off % BSIZE);
    memmove(dst + tot, bp->data + off % BSIZE, m);
    brelse(bp);
  }

  return tot;
}

int
block_write_bytes(uint dev, uint off, uchar *src, uint n)
{
  struct buf *bp;
  uint m, tot;

  if(src == 0 && n != 0)
    return -1;

  for(tot = 0; tot < n; tot += m, off += m){
    bp = bread(dev, off / BSIZE);
    m = block_min(n - tot, BSIZE - off % BSIZE);
    memmove(bp->data + off % BSIZE, src + tot, m);
    bwrite(bp);
    brelse(bp);
  }

  return tot;
}
