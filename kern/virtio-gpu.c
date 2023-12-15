#include "virtio-gpu.h"
#include <inc/stdio.h>
#include <kern/pcireg.h>

#define VIRTIO_PCI_FEATURE_REGISTER 0x0 
#define VIRTIO_PCI_STATUS_REGISTER  0x12 
#define VIRTIO_PCI_ACK_BIT          0x00000001 
#define VIRTIO_PCI_DRV_BIT          0x00000002

#define PCI_BAR_MMIO_BA (((1U << 28)-1) << 4)

#define VIRTIO_STATUS_FEATURES_OK 0x8

// Common configuration
#define VIRTIO_PCI_CAP_COMMON_CFG   1

// Notifications
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2

// ISR Status
#define VIRTIO_PCI_CAP_ISR_CFG      3

// Device specific configuration
#define VIRTIO_PCI_CAP_DEVICE_CFG   4

// PCI configuration access
#define VIRTIO_PCI_CAP_PCI_CFG      5

#define atomic_ld_acq(value) \
    __atomic_load_n(value, __ATOMIC_ACQUIRE)

struct virtio_pci_cap_hdr_t {
    uint8_t cap_vendor;
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t type;
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
};

struct pci_config_hdr_t {
    // 0x00
    uint16_t vendor;
    uint16_t device;

    // 0x04
    uint16_t command;
    uint16_t status;

    // 0x08
    uint8_t revision;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t dev_class;

    // 0x0C
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;

    // 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24
    uint32_t base_addr[6];

    // 0x28
    uint32_t cardbus_cis_ptr;

    // 0x2C
    uint16_t subsystem_vendor;
    uint16_t subsystem_id;

    // 0x30
    uint32_t expansion_rom_addr;

    // 0x34
    uint8_t capabilities_ptr;

    // 0x35howe
    uint8_t reserved[7];

    // 0x3C
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t min_grant;
    uint8_t max_latency;
};

struct virtio_pci_common_cfg_t {
    // About the whole device

    // read-write
    uint32_t device_feature_select;

    // read-only for driver
    uint32_t device_feature;

    // read-write
    uint32_t driver_feature_select;

    // read-write
    uint32_t driver_feature;

    // read-write
    uint16_t config_msix_vector;

    // read-only for driver
    uint16_t num_queues;

    // read-write
    uint8_t device_status;

    // read-only for driver
    uint8_t config_generation;

    // About a specific virtqueue

    // read-write
    uint16_t queue_select;

    // read-write, power of 2, or 0
    uint16_t queue_size;

    // read-write
    uint16_t queue_msix_vector;

    // read-write
    uint16_t queue_enable;

    // read-only for driver
    uint16_t queue_notify_off;

    // read-write
    uint64_t queue_desc;

    // read-write
    uint64_t queue_avail;

    // read-write
    uint64_t queue_used;
};

uint32_t base_addr[6];

// bool is_bar_mmio(size_t bar)
// {
//     return PCI_BAR_RTE_GET(base_addr[bar]) == 0;
// }

static uint64_t get_bar(size_t bar)
{
    uint64_t addr;

    addr = base_addr[bar] & -4;

    return addr;
}

static uint8_t get_capabilities_ptr(struct pci_func *pcif) {
    uint32_t reg = pci_conf_read_sized(pcif, 0x34, sizeof(uint8_t));
    return PCI_CAPLIST_PTR(reg);
}

static void parse_common_cfg(struct pci_func *pcif, struct virtio_pci_cap_hdr_t* cap_header) {
    uint32_t addr = cap_header->offset + get_bar(cap_header->bar);
    cprintf(" offset = %u bar base = %lu common cfg addr = %u", cap_header->offset, get_bar(cap_header->bar), addr);



    // Reset device
    pci_conf_write_sized(pcif, addr + VIRTIO_PCI_STATUS_REGISTER, sizeof(uint8_t), 0);

    // Wait until reset completes
    while (pci_conf_read_sized(pcif, addr + VIRTIO_PCI_STATUS_REGISTER, sizeof(uint8_t)) != 0) {
        asm volatile("pause");
    }

    uint8_t status;
    
    // Set ACK bit (we recognised this device)
    status = pci_conf_read_sized(pcif, addr + VIRTIO_PCI_STATUS_REGISTER, sizeof(uint8_t));
    pci_conf_write_sized(pcif, addr + VIRTIO_PCI_STATUS_REGISTER, sizeof(uint8_t), status | VIRTIO_PCI_ACK_BIT);

    // Set DRIVER bit (we have driver for this device)
    status = pci_conf_read_sized(pcif, addr + VIRTIO_PCI_STATUS_REGISTER, sizeof(uint8_t));
    pci_conf_write_sized(pcif, addr + VIRTIO_PCI_STATUS_REGISTER, sizeof(uint8_t), status | VIRTIO_PCI_DRV_BIT);

    // Set FEATURES_OK flag
    status = pci_conf_read_sized(pcif, addr + VIRTIO_PCI_STATUS_REGISTER, sizeof(uint8_t));
    pci_conf_write_sized(pcif, addr + VIRTIO_PCI_STATUS_REGISTER, sizeof(uint8_t), status | VIRTIO_STATUS_FEATURES_OK);
}

void init_gpu(struct pci_func *pcif) {
    cprintf("GPU Init");

    for (int i = 0; i < 6; ++i) {
        base_addr[i] = pci_conf_read(pcif, 0x10 + i*4);
    }

    uint8_t cap_offset = get_capabilities_ptr(pcif);

    cprintf(" cap offset = %d", (int)cap_offset);

    while (cap_offset != 0) {
        struct virtio_pci_cap_hdr_t cap_header;
        cap_header.cap_vendor = 0;
        while (cap_header.cap_vendor != PCI_CAP_VENDSPEC) {
            pci_memcpy_from(pcif, cap_offset, (uint8_t *)&cap_header, sizeof(cap_header));
            cap_offset = cap_header.cap_next;
        }


        switch (cap_header.type) {
            case VIRTIO_PCI_CAP_COMMON_CFG: parse_common_cfg(pcif, &cap_header); break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG: break;
            case VIRTIO_PCI_CAP_ISR_CFG: break;
            case VIRTIO_PCI_CAP_DEVICE_CFG: break;
            case VIRTIO_PCI_CAP_PCI_CFG: break;
            default: break;
        }

        cap_offset = cap_header.cap_next;
    }

    // // Clear status register
    // pci_conf_write(pcif, VIRTIO_PCI_STATUS_REGISTER, 0);
    
    // // Set ACK bit (we recognised this device)
    // pci_conf_write(pcif, VIRTIO_PCI_STATUS_REGISTER, VIRTIO_PCI_ACK_BIT);

    // // Set DRIVER bit (we have driver for this device)
    // pci_conf_write(pcif, VIRTIO_PCI_STATUS_REGISTER, VIRTIO_PCI_DRV_BIT);

    // // Get supported features
    // uint32_t features = pci_conf_read(pcif, VIRTIO_PCI_FEATURE_REGISTER);
    // cprintf(" GPU FEATURES = 0x%x", features);

    cprintf("\n");
}