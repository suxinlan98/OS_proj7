// SPDX-License-Identifier: BSD-2-Clause
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

void
hal_intr_init(void)
{
  plicinit();
}

void
hal_intr_inithart(void)
{
  plicinithart();
}

int
hal_irq_claim(void)
{
  return plic_claim();
}

void
hal_irq_complete(int irq)
{
  plic_complete(irq);
}

void
hal_disk_init(void)
{
  virtio_disk_init();
}

void
hal_disk_rw(struct buf *b, int write)
{
  virtio_disk_rw(b, write);
}
