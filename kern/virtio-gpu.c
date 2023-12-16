#include "virtio-gpu.h"
#include <inc/stdio.h>
#include <kern/pcireg.h>
#include <kern/pci.bits.h>
#include <inc/string.h>

void
map_addr_early_boot(uintptr_t va, uintptr_t pa, size_t sz);

// ---------------------------------------------------------------------------------------------------------------------
// defines

#define atomic_ld_acq(value) \
    __atomic_load_n(value, __ATOMIC_ACQUIRE)

#define GPU_MMIO_ADDR_START 0x000000c000001000
#define GPU_MMIO_SIZE       0x4000

// ---------------------------------------------------------------------------------------------------------------------
// BAR function

static bool is_bar_mmio(uint32_t *base_addrs, size_t bar) {
    return PCI_BAR_RTE_GET(base_addrs[bar]) == 0;
}

static bool is_bar_64bit(uint32_t *base_addrs, size_t bar) {
    return (PCI_BAR_RTE_GET(base_addrs[bar]) == 0) &&
            (PCI_BAR_MMIO_TYPE_GET(base_addrs[bar]) == PCI_BAR_MMIO_TYPE_64BIT);
}

static uint64_t get_bar(uint32_t *base_addrs, size_t bar) {
    uint64_t addr;

    if (is_bar_mmio(base_addrs, bar)) {
        addr = base_addrs[bar] & PCI_BAR_MMIO_BA;

        if (is_bar_64bit(base_addrs, bar)) {
            addr |= ((uint64_t)(base_addrs[bar + 1])) << 32;
        }
    } else {
        // Mask off low 2 bits
        addr = base_addrs[bar] & -4;
    }

    return addr;
}

// ---------------------------------------------------------------------------------------------------------------------

static uint8_t get_capabilities_ptr(struct pci_func *pcif) {
    return pci_conf_read_sized(pcif, PCI_CAPLISTPTR_REG, sizeof(uint8_t));
}

// ---------------------------------------------------------------------------------------------------------------------

static void parse_common_cfg(struct pci_func *pcif, volatile struct virtio_pci_common_cfg_t* cfg_header) {
    // Reset device
    cfg_header->device_status = 0;

    // Wait until reset completes
    while (atomic_ld_acq(&cfg_header->device_status) != 0) {
        asm volatile("pause");
    }

    // Set ACK bit (we recognised this device)
    cfg_header->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;

    // Set DRIVER bit (we have driver for this device)
    cfg_header->device_status |= VIRTIO_STATUS_DRIVER;
    
    // Get features (we accept all, lol)
    // 4 is from dgos and maybe it is overkill, but not incorrect
    for (int i = 0; i < 4; ++i) {
        cfg_header->device_feature_select = i;
        uint64_t features = cfg_header->device_feature;
        cfg_header->driver_feature_select = i;
        cfg_header->driver_feature = features;
    }

    // Say that we are ready to ckeck featurus
    cfg_header->device_status |= VIRTIO_STATUS_FEATURES_OK;

    // Check if they are OK with this feature set
    if (!(cfg_header->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        cfg_header->device_status |= VIRTIO_STATUS_FAILED;
        cprintf("FAILED TO SETUP GPU: Feature error");
        return;
    }

    // Set DRIVER_OK flag
    cfg_header->device_status |= VIRTIO_STATUS_DRIVER_OK;
}

// ---------------------------------------------------------------------------------------------------------------------

void init_gpu(struct pci_func *pcif) {
    // Map GPU MMIO
    map_addr_early_boot(GPU_MMIO_ADDR_START, GPU_MMIO_ADDR_START, GPU_MMIO_SIZE);

    // PCI BAR's
    uint32_t base_addrs[6];

    // Get base_addrs from pci header
    for (int i = 0; i < 6; ++i) {
        base_addrs[i] = pci_conf_read(pcif, 0x10 + i*4);
    }

    // Get Capability List pointer
    uint8_t cap_offset = get_capabilities_ptr(pcif);

    // Iterate over capabilities and parse them
    while (cap_offset != 0) {
        struct virtio_pci_cap_hdr_t cap_header;
        cap_header.cap_vendor = 0;

        // Search for vendor specific header
        while (cap_header.cap_vendor != PCI_CAP_VENDSPEC) {
            pci_memcpy_from(pcif, cap_offset, (uint8_t *)&cap_header, sizeof(cap_header));
            cap_offset = cap_header.cap_next;
        }

        uint64_t addr;

        switch (cap_header.type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                addr = cap_header.offset + get_bar(base_addrs, cap_header.bar);
                parse_common_cfg(pcif, (volatile struct virtio_pci_common_cfg_t*)addr);
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG: break;
            case VIRTIO_PCI_CAP_ISR_CFG: break;
            case VIRTIO_PCI_CAP_DEVICE_CFG: break;
            case VIRTIO_PCI_CAP_PCI_CFG: break;
            default: break;
        }

        cap_offset = cap_header.cap_next;
    }
}