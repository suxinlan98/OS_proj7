#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "loongarch.h"
#include "defs.h"

void
apic_init(void)
{
  uint64 mask = (0x1UL << UART0_IRQ) | (0x1UL << VIRTIO0_IRQ);

  *(volatile uint64*)(LS7A_INT_MASK_REG) = ~mask;

  *(volatile uint64*)(LS7A_INT_EDGE_REG) = (0x1UL << UART0_IRQ);

  *(volatile uint8*)(LS7A_INT_HTMSI_VEC_REG + UART0_IRQ) = UART0_IRQ;
  *(volatile uint8*)(LS7A_INT_HTMSI_VEC_REG + VIRTIO0_IRQ) = VIRTIO0_IRQ;

  *(volatile uint64*)(LS7A_INT_POL_REG) = 0x0UL;

}

void
apicinit(void)
{
  apic_init();
}

void
apic_complete(uint64 irq)
{
  *(volatile uint64*)(LS7A_INT_CLEAR_REG) = (irq);
}
