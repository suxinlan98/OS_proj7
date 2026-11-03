//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

#ifdef ARCH_LOONGARCH
void virtio_pci_init(void);
uint8 virtio_pci_get_status(void);
void virtio_pci_set_status(uint8);
uint64 virtio_pci_get_features(void);
void virtio_pci_set_features(uint64);
void virtio_pci_select_queue(uint16);
uint16 virtio_pci_queue_max(void);
uint16 virtio_pci_queue_ready(void);
void virtio_pci_set_queue_num(uint16);
void virtio_pci_set_queue_desc(uint64);
void virtio_pci_set_queue_avail(uint64);
void virtio_pci_set_queue_used(uint64);
void virtio_pci_set_queue_ready(uint16);
void virtio_pci_notify(uint16);
void virtio_pci_intr_ack(void);
#else
// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))
#endif

static uint64
virtio_dma_addr(void *addr)
{
#ifdef ARCH_LOONGARCH
  return ((uint64)addr) & ~DMWIN_MASK;
#else
  return (uint64)addr;
#endif
}

static uint8
virtio_get_status(void)
{
#ifdef ARCH_LOONGARCH
  return virtio_pci_get_status();
#else
  return *R(VIRTIO_MMIO_STATUS);
#endif
}

static void
virtio_set_status(uint8 status)
{
#ifdef ARCH_LOONGARCH
  virtio_pci_set_status(status);
#else
  *R(VIRTIO_MMIO_STATUS) = status;
#endif
}

static uint64
virtio_get_features(void)
{
#ifdef ARCH_LOONGARCH
  return virtio_pci_get_features();
#else
  return *R(VIRTIO_MMIO_DEVICE_FEATURES);
#endif
}

static void
virtio_set_features(uint64 features)
{
#ifdef ARCH_LOONGARCH
  virtio_pci_set_features(features);
#else
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;
#endif
}

static void
virtio_select_queue(uint16 queue)
{
#ifdef ARCH_LOONGARCH
  virtio_pci_select_queue(queue);
#else
  *R(VIRTIO_MMIO_QUEUE_SEL) = queue;
#endif
}

static uint16
virtio_queue_max(void)
{
#ifdef ARCH_LOONGARCH
  return virtio_pci_queue_max();
#else
  return *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
#endif
}

static uint16
virtio_queue_ready(void)
{
#ifdef ARCH_LOONGARCH
  return virtio_pci_queue_ready();
#else
  return *R(VIRTIO_MMIO_QUEUE_READY);
#endif
}

static void
virtio_setup_queue(uint16 queue, uint16 num, void *desc, void *avail, void *used)
{
#ifdef ARCH_LOONGARCH
  virtio_pci_select_queue(queue);
  virtio_pci_set_queue_num(num);
  virtio_pci_set_queue_desc(virtio_dma_addr(desc));
  virtio_pci_set_queue_avail(virtio_dma_addr(avail));
  virtio_pci_set_queue_used(virtio_dma_addr(used));
#else
  *R(VIRTIO_MMIO_QUEUE_NUM) = num;
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = virtio_dma_addr(desc);
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = virtio_dma_addr(desc) >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = virtio_dma_addr(avail);
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = virtio_dma_addr(avail) >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = virtio_dma_addr(used);
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = virtio_dma_addr(used) >> 32;
#endif
}

static void
virtio_set_queue_ready(uint16 ready)
{
#ifdef ARCH_LOONGARCH
  virtio_pci_set_queue_ready(ready);
#else
  *R(VIRTIO_MMIO_QUEUE_READY) = ready;
#endif
}

static void
virtio_notify(uint16 queue)
{
#ifdef ARCH_LOONGARCH
  virtio_pci_notify(queue);
#else
  (void)queue;
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
#endif
}

static void
virtio_intr_ack(void)
{
#ifdef ARCH_LOONGARCH
  virtio_pci_intr_ack();
#else
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
#endif
}

static struct disk {
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  struct virtio_blk_req ops[NUM];
  
  struct spinlock vdisk_lock;
  
} disk;

void
virtio_disk_init(void)
{
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");

#ifdef ARCH_LOONGARCH
  virtio_pci_init();
#else
  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
#endif
  
  // reset device
  virtio_set_status(status);
#ifdef ARCH_LOONGARCH
  while(virtio_get_status() != 0)
    ;
#endif

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  virtio_set_status(status);

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  virtio_set_status(status);

  // negotiate features
  uint64 features = virtio_get_features();
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
#ifdef ARCH_LOONGARCH
  features &= (1ULL << VIRTIO_F_VERSION_1);
#endif
  virtio_set_features(features);

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  virtio_set_status(status);

  // re-read status to ensure FEATURES_OK is set.
  status = virtio_get_status();
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  virtio_select_queue(0);

  // ensure queue 0 is not in use.
  if(virtio_queue_ready()){
#ifdef ARCH_LOONGARCH
    virtio_set_queue_ready(0);
#else
    panic("virtio disk should not be ready");
#endif
  }

  // check maximum queue size.
  uint32 max = virtio_queue_max();
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // set queue size and addresses.
  virtio_setup_queue(0, NUM, disk.desc, disk.avail, disk.used);

  // queue is ready.
  virtio_set_queue_ready(0x1);

  // all NUM descriptors start out unused.
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  virtio_set_status(status);

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}

// free a chain of descriptors.
static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

void
virtio_disk_rw(struct buf *b, int write)
{
  uint64 blockno = b->blockno;

  if(b->dev == EXT2DEV)
    blockno += FSSIZE;
  else if(b->dev == FAT32DEV)
    blockno += FAT32START;

  uint64 sector = blockno * (BSIZE / 512);

  acquire(&disk.vdisk_lock);

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      break;
    }
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = virtio_dma_addr(buf0);
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = virtio_dma_addr(b->data);
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    disk.desc[idx[1]].flags = 0; // device reads b->data
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff; // device writes 0 on success
  disk.desc[idx[2]].addr = virtio_dma_addr(&disk.info[idx[0]].status);
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  disk.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  disk.info[idx[0]].b = b;

  // tell the device the first index in our chain of descriptors.
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  disk.avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  virtio_notify(0); // value is queue number

  // Wait for virtio_disk_intr() to say request has finished.
  while(b->disk == 1) {
    sleep(b, &disk.vdisk_lock);
  }

  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);

  release(&disk.vdisk_lock);
}

void
virtio_disk_intr()
{
  acquire(&disk.vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  virtio_intr_ack();

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.

  while(disk.used_idx != disk.used->idx){
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if(disk.info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = disk.info[id].b;
    b->disk = 0;   // disk is done with buf
    wakeup(b);

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);
}

// Nonzero if the device has posted used-ring completions that the
// driver has not processed yet.  Used by trap code to retry after
// acknowledging the interrupt, because with legacy INTx signalling a
// completion that arrives mid-acknowledge can be swallowed by the
// interrupt-controller chain without raising a new interrupt.
int
virtio_disk_pending(void)
{
  __sync_synchronize();
  return disk.used_idx != disk.used->idx;
}

// Backstop for lost completion interrupts (observed with LoongArch
// legacy INTx: the controller chain can swallow a completion that
// arrives while the previous one is being acknowledged, so no further
// interrupt is ever raised).  Called from the timer interrupt, which
// does not depend on that chain; drains pending completions unless
// the disk lock is busy somewhere else, in which case the next tick
// retries.
void
virtio_disk_watchdog(void)
{
  if(disk.vdisk_lock.locked)
    return;
  if(!virtio_disk_pending())
    return;
  virtio_disk_intr();
}
