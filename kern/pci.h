#ifndef JOS_KERN_PCI_H
#define JOS_KERN_PCI_H

#include <inc/types.h>

// PCI subsystem interface
enum { pci_res_bus, pci_res_mem, pci_res_io, pci_res_max };

struct pci_bus;

struct pci_func {
    struct pci_bus *bus;	// Primary bus for bridges

    uint32_t dev;
    uint32_t func;

    uint32_t dev_id;
    uint32_t dev_class;

    uint32_t reg_base[6];
    uint32_t reg_size[6];
    uint8_t irq_line;
};

struct pci_bus {
    struct pci_func *parent_bridge;
    uint32_t busno;
};

int  pci_init(void);
void pci_func_enable(struct pci_func *f);

uint32_t pci_conf_read(struct pci_func *f, uint32_t off);
void pci_conf_write(struct pci_func *f, uint32_t off, uint32_t v);

uint32_t pci_conf_read_sized(struct pci_func *f, uint32_t off, uint32_t size);
void pci_conf_write_sized(struct pci_func *f, uint32_t off, uint32_t size, uint32_t v);
void pci_memcpy_from(struct pci_func *f, uint32_t off, uint8_t *dest, uint32_t size);

#endif
