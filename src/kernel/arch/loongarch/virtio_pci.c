#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "loongarch.h"
#include "defs.h"
#include "virtio.h"

#define PCI_VENDOR_ID          0x00
#define PCI_DEVICE_ID          0x02
#define PCI_COMMAND            0x04
#define PCI_CAPABILITY_LIST    0x34
#define PCI_BAR0               0x10

#define PCI_COMMAND_IO         0x1
#define PCI_COMMAND_MEMORY     0x2
#define PCI_COMMAND_MASTER     0x4

#define PCI_VENDOR_VIRTIO      0x1af4
#define PCI_DEVICE_VIRTIO_BLK  0x1042

#define PCI_CAP_ID_VNDR        0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3

struct virtio_pci_common_cfg {
  uint32 device_feature_select;
  uint32 device_feature;
  uint32 driver_feature_select;
  uint32 driver_feature;
  uint16 msix_config;
  uint16 num_queues;
  uint8 device_status;
  uint8 config_generation;
  uint16 queue_select;
  uint16 queue_size;
  uint16 queue_msix_vector;
  uint16 queue_enable;
  uint16 queue_notify_off;
  uint64 queue_desc;
  uint64 queue_driver;
  uint64 queue_device;
};

static volatile struct virtio_pci_common_cfg *common_cfg;
static volatile uint8 *isr_cfg;
static volatile uint16 *notify_cfg;
static uint32 notify_off_multiplier;

static uint busno, devno, funcno;
static uint64 bar_addr[6];
static uint64 next_mmio = PCI_MMIO_BASE & ~DMWIN_MASK;

static uint64
pci_conf_addr(uint bus, uint dev, uint func, uint off)
{
  return PCI_ECAM + (bus << 20) + (dev << 15) + (func << 12) + off;
}

static uint8
pci_read8(uint bus, uint dev, uint func, uint off)
{
  return *(volatile uint8*)pci_conf_addr(bus, dev, func, off);
}

static uint16
pci_read16(uint bus, uint dev, uint func, uint off)
{
  return *(volatile uint16*)pci_conf_addr(bus, dev, func, off);
}

static uint32
pci_read32(uint bus, uint dev, uint func, uint off)
{
  return *(volatile uint32*)pci_conf_addr(bus, dev, func, off);
}

static void
pci_write16(uint bus, uint dev, uint func, uint off, uint16 val)
{
  *(volatile uint16*)pci_conf_addr(bus, dev, func, off) = val;
}

static void
pci_write32(uint bus, uint dev, uint func, uint off, uint32 val)
{
  *(volatile uint32*)pci_conf_addr(bus, dev, func, off) = val;
}

static uint64
align_up(uint64 value, uint64 align)
{
  return (value + align - 1) & ~(align - 1);
}

static uint64
alloc_mmio(uint64 size)
{
  uint64 addr;

  size = align_up(size, PGSIZE);
  addr = align_up(next_mmio, size);
  if(addr + size > (PCI_MMIO_BASE & ~DMWIN_MASK) + PCI_MMIO_SIZE)
    panic("virtio pci mmio");
  next_mmio = addr + size;
  return addr;
}

static void
alloc_bars(uint bus, uint dev, uint func)
{
  for(int i = 0; i < 6; i++){
    uint off = PCI_BAR0 + i * 4;
    uint32 old = pci_read32(bus, dev, func, off);

    pci_write32(bus, dev, func, off, 0xffffffff);
    uint32 low = pci_read32(bus, dev, func, off);
    pci_write32(bus, dev, func, off, old);

    if(low == 0 || low == 0xffffffff)
      continue;
    if(low & 0x1){
      pci_write32(bus, dev, func, off, old);
      continue;
    }

    uint64 mask;
    uint64 size;
    uint64 addr;
    int type = (low >> 1) & 0x3;

    if(type == 0x2 && i + 1 < 6){
      uint32 oldhi = pci_read32(bus, dev, func, off + 4);
      pci_write32(bus, dev, func, off + 4, 0xffffffff);
      uint32 high = pci_read32(bus, dev, func, off + 4);
      pci_write32(bus, dev, func, off + 4, oldhi);
      mask = ((uint64)high << 32) | (low & ~0xf);
      size = -mask;
      addr = alloc_mmio(size);
      pci_write32(bus, dev, func, off, (uint32)addr);
      pci_write32(bus, dev, func, off + 4, (uint32)(addr >> 32));
      bar_addr[i] = addr | DMWIN_MASK;
      i++;
    } else {
      mask = low & ~0xf;
      size = (uint32)(-mask);
      addr = alloc_mmio(size);
      pci_write32(bus, dev, func, off, (uint32)addr);
      bar_addr[i] = addr | DMWIN_MASK;
    }
  }
}

static void
find_device(void)
{
  for(uint dev = 0; dev < 32; dev++){
    uint16 vendor = pci_read16(0, dev, 0, PCI_VENDOR_ID);
    uint16 device = pci_read16(0, dev, 0, PCI_DEVICE_ID);
    if(vendor == PCI_VENDOR_VIRTIO && device == PCI_DEVICE_VIRTIO_BLK){
      busno = 0;
      devno = dev;
      funcno = 0;
      return;
    }
  }
  panic("virtio pci disk");
}

static uint64
cap_addr(uint8 bar, uint32 off)
{
  if(bar >= 6 || bar_addr[bar] == 0)
    panic("virtio pci cap bar");
  return bar_addr[bar] + off;
}

static void
parse_caps(void)
{
  uint8 cap = pci_read8(busno, devno, funcno, PCI_CAPABILITY_LIST);

  while(cap){
    uint8 id = pci_read8(busno, devno, funcno, cap);
    uint8 next = pci_read8(busno, devno, funcno, cap + 1);

    if(id == PCI_CAP_ID_VNDR){
      uint8 cfg_type = pci_read8(busno, devno, funcno, cap + 3);
      uint8 bar = pci_read8(busno, devno, funcno, cap + 4);
      uint32 offset = pci_read32(busno, devno, funcno, cap + 8);

      if(cfg_type == VIRTIO_PCI_CAP_COMMON_CFG){
        common_cfg = (volatile struct virtio_pci_common_cfg*)cap_addr(bar, offset);
      } else if(cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG){
        notify_cfg = (volatile uint16*)cap_addr(bar, offset);
        notify_off_multiplier = pci_read32(busno, devno, funcno, cap + 16);
      } else if(cfg_type == VIRTIO_PCI_CAP_ISR_CFG){
        isr_cfg = (volatile uint8*)cap_addr(bar, offset);
      }
    }
    cap = next;
  }

  if(common_cfg == 0 || notify_cfg == 0 || isr_cfg == 0)
    panic("virtio pci caps");
}

void
virtio_pci_init(void)
{
  find_device();
  alloc_bars(busno, devno, funcno);
  parse_caps();

  pci_write16(busno, devno, funcno, PCI_COMMAND,
              PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
}

uint8
virtio_pci_get_status(void)
{
  return common_cfg->device_status;
}

void
virtio_pci_set_status(uint8 status)
{
  common_cfg->device_status = status;
}

uint64
virtio_pci_get_features(void)
{
  uint64 features;

  common_cfg->device_feature_select = 0;
  features = common_cfg->device_feature;
  common_cfg->device_feature_select = 1;
  features |= ((uint64)common_cfg->device_feature) << 32;
  return features;
}

void
virtio_pci_set_features(uint64 features)
{
  common_cfg->driver_feature_select = 0;
  common_cfg->driver_feature = features;
  common_cfg->driver_feature_select = 1;
  common_cfg->driver_feature = features >> 32;
}

void
virtio_pci_select_queue(uint16 queue)
{
  common_cfg->queue_select = queue;
}

uint16
virtio_pci_queue_max(void)
{
  return common_cfg->queue_size;
}

uint16
virtio_pci_queue_ready(void)
{
  return common_cfg->queue_enable;
}

void
virtio_pci_set_queue_num(uint16 num)
{
  common_cfg->queue_size = num;
}

void
virtio_pci_set_queue_desc(uint64 addr)
{
  common_cfg->queue_desc = addr;
}

void
virtio_pci_set_queue_avail(uint64 addr)
{
  common_cfg->queue_driver = addr;
}

void
virtio_pci_set_queue_used(uint64 addr)
{
  common_cfg->queue_device = addr;
}

void
virtio_pci_set_queue_ready(uint16 ready)
{
  common_cfg->queue_msix_vector = 0xffff;
  common_cfg->queue_enable = ready;
}

void
virtio_pci_notify(uint16 queue)
{
  uint16 off;

  common_cfg->queue_select = queue;
  off = common_cfg->queue_notify_off;
  *(volatile uint16*)((uint64)notify_cfg + off * notify_off_multiplier) = queue;
}

void
virtio_pci_intr_ack(void)
{
  (void)*isr_cfg;
}
