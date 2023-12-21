#include "virtio-gpu.h"
#include "virtio-queue.h"
#include <inc/stdio.h>
#include <kern/pcireg.h>
#include <kern/pmap.h>
#include <kern/pci.bits.h>
#include <inc/string.h>

struct virtq controlq; // queue for sending control commands
struct virtq cursorq;  // queue for sending cursor updates

void
map_addr_early_boot(uintptr_t va, uintptr_t pa, size_t sz);

// ---------------------------------------------------------------------------------------------------------------------
// defines

#define atomic_ld_acq(value) \
    __atomic_load_n(value, __ATOMIC_ACQUIRE)

#define atomic_st_rel(value, rhs) \
    __atomic_store_n((value), (rhs), __ATOMIC_RELEASE)

#define GPU_MMIO_ADDR_START 0x000000c000001000
#define GPU_MMIO_SIZE       0x4000

// ---------------------------------------------------------------------------------------------------------------------
// BAR function

static bool
is_bar_mmio(uint32_t *base_addrs, size_t bar) {
    return PCI_BAR_RTE_GET(base_addrs[bar]) == 0;
}

static bool
is_bar_64bit(uint32_t *base_addrs, size_t bar) {
    return (PCI_BAR_RTE_GET(base_addrs[bar]) == 0) &&
           (PCI_BAR_MMIO_TYPE_GET(base_addrs[bar]) == PCI_BAR_MMIO_TYPE_64BIT);
}

static uint64_t
get_bar(uint32_t *base_addrs, size_t bar) {
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

static uint8_t
get_capabilities_ptr(struct pci_func *pcif) {
    return pci_conf_read_sized(pcif, PCI_CAPLISTPTR_REG, sizeof(uint8_t));
}

// ---------------------------------------------------------------------------------------------------------------------

static void
parse_common_cfg(struct pci_func *pcif, volatile struct virtio_pci_common_cfg_t *cfg_header) {
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

    // Config two queues

    cfg_header->queue_select = CURSOR_VIRTQ;
    cfg_header->queue_desc  = (uint64_t)PADDR(&cursorq.desc);
    cfg_header->queue_avail = (uint64_t)PADDR(&cursorq.desc);
    cfg_header->queue_used  = (uint64_t)PADDR(&cursorq.used);
    cfg_header->queue_enable = 1;
    cursorq.log2_size = 6;

    cfg_header->queue_select = CONTROL_VIRTQ;
    cfg_header->queue_desc  = (uint64_t)PADDR(&controlq.desc);
    cfg_header->queue_avail = (uint64_t)PADDR(&controlq.avail);
    cfg_header->queue_used  = (uint64_t)PADDR(&controlq.used);
    cfg_header->queue_enable = 1;
    controlq.log2_size = 6;

    cprintf("controlq.desc %lx\n", (uint64_t)&controlq.desc);

    // Set DRIVER_OK flag
    cfg_header->device_status |= VIRTIO_STATUS_DRIVER_OK;
}

// ---------------------------------------------------------------------------------------------------------------------

void
init_gpu(struct pci_func *pcif) {
    // Map GPU MMIO
    map_addr_early_boot(GPU_MMIO_ADDR_START, GPU_MMIO_ADDR_START, GPU_MMIO_SIZE);

    // PCI BAR's
    uint32_t base_addrs[6];

    // Get base_addrs from pci header
    for (int i = 0; i < 6; ++i) {
        base_addrs[i] = pci_conf_read(pcif, 0x10 + i * 4);
    }

    // Get Capability List pointer
    uint8_t cap_offset = get_capabilities_ptr(pcif);
    uint8_t cap_offset_old = cap_offset;
    uint64_t notify_cap_size = 0;
    uint32_t notify_cap_offset = 0;
    uint32_t notify_off_multiplier = 0;

    // Iterate over capabilities and parse them
    while (cap_offset != 0) {
        struct virtio_pci_cap_hdr_t cap_header;
        cap_header.cap_vendor = 0;

        // Search for vendor specific header
        while (cap_header.cap_vendor != PCI_CAP_VENDSPEC) {
            pci_memcpy_from(pcif, cap_offset, (uint8_t *)&cap_header, sizeof(cap_header));
            cap_offset_old = cap_offset;
            cap_offset = cap_header.cap_next;
        }

        uint64_t addr;

        uint64_t notify_reg;
        uint64_t bar_addr;

        volatile struct virtio_pci_common_cfg_t *common_cfg_ptr;

        switch (cap_header.type) {
        case VIRTIO_PCI_CAP_COMMON_CFG:
            bar_addr = get_bar(base_addrs, cap_header.bar);
            addr = cap_header.offset + bar_addr;
            common_cfg_ptr = (volatile struct virtio_pci_common_cfg_t *)addr;
            notify_reg = notify_cap_offset + common_cfg_ptr->queue_notify_off * notify_off_multiplier;
            controlq.notify_reg += notify_reg;

            cprintf("queue_notify_off 0x%x\n", common_cfg_ptr->queue_notify_off);

            if (notify_cap_size < common_cfg_ptr->queue_notify_off * notify_off_multiplier + 2) {
                cprintf("Wrong size\n");
            }
            parse_common_cfg(pcif, common_cfg_ptr);
            break;
        case VIRTIO_PCI_CAP_NOTIFY_CFG:
            if (notify_cap_size) {
                break;
            }
            notify_cap_size = cap_header.length;
            notify_cap_offset = cap_header.offset;

            cprintf("upgrading notify_reg from %lx ", controlq.notify_reg);
            controlq.notify_reg += get_bar(base_addrs, cap_header.bar);
            cprintf("to %lx at ptr %p\n", controlq.notify_reg, &controlq.notify_reg);

            pci_memcpy_from(pcif, cap_offset_old + sizeof(cap_header),
                            (uint8_t *)&notify_off_multiplier, sizeof(notify_off_multiplier));

            cprintf("notify off mul 0x%x\n", notify_off_multiplier);
            cprintf("notify cap size %ld\n", notify_cap_size);
            break;
        case VIRTIO_PCI_CAP_ISR_CFG: break;
        case VIRTIO_PCI_CAP_DEVICE_CFG: break;
        case VIRTIO_PCI_CAP_PCI_CFG: break;
        default: break;
        }

        cap_offset = cap_header.cap_next;
    }

    get_display_info();
}


static void
notify_queue(struct virtq *queue, uint16_t queue_idx) {
    cprintf("(B) notify reg 0x%lx at %p\n", queue->notify_reg, &queue->notify_reg);
    *((uint64_t *)queue->notify_reg) = queue_idx;
    cprintf("(A) notify reg 0x%lx at %p\n", queue->notify_reg, &queue->notify_reg);
}

static void
queue_avail(struct virtq *queue, uint32_t count) {
    uint32_t mask = ~-(1 << queue->log2_size);

    bool skip = false;
    uint32_t avail_head = queue->avail.idx;
    uint32_t chain_start = ~((uint32_t)0);
    for (uint32_t i = 0; i < count; ++i) {
        if (!skip) {
            chain_start = i;
        }

        skip = queue->desc[i].flags & VIRTQ_DESC_F_NEXT;

        // Write an entry to the avail ring telling virtio to
        // look for a chain starting at chain_start, if this is the end
        // of a chain (end because next is false)
        if (!skip && chain_start != ~((uint32_t)0))
            queue->avail.ring[avail_head++ & mask] = chain_start;
    }
    cprintf("avail head %d\n", avail_head);
    atomic_st_rel(&queue->avail.used_event, avail_head - 1);

    // Update idx (tell virtio where we would put the next new item)
    // enforce ordering until after prior store is globally visible
    // queue->avail.idx = 100;
    // queue->used.idx = 100;
    atomic_st_rel(&queue->avail.idx, avail_head);
}

static void
send_and_recieve(struct virtq *queue, uint16_t queue_idx, void *to_send, uint64_t send_size, void *to_recieve, uint64_t recieve_size) {
    // TODO rewrite
    queue->desc[0].addr = (uint64_t)PADDR(to_send);
    queue->desc[0].len = send_size;
    queue->desc[0].flags = 0;
    queue->desc[0].next = -1;
    cprintf("&queue->desc[0].addr: %p\n", (void *)&queue->desc[0].addr);
    cprintf("queue->desc[0].addr %lx to send: %lx\n",queue->desc[0].addr, (uint64_t)to_send);

    queue->desc[1].addr = (uint64_t)PADDR(to_recieve);
    queue->desc[1].len = recieve_size;
    queue->desc[1].flags = VIRTQ_DESC_F_WRITE;
    queue->desc[1].next = -1;

    queue_avail(queue, 2);
    notify_queue(queue, queue_idx);
}

struct virtio_gpu_ctrl_hdr display_info;
struct virtio_gpu_resp_display_info res;

void
get_display_info() {
    memset(&display_info, 0, sizeof(display_info));
    display_info.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    memset(&res, 0, sizeof(res));

    send_and_recieve(&controlq, 0, &display_info, sizeof(display_info), &res, sizeof(res));
    // send_and_recieve(&cursorq, 1, &display_info, sizeof(display_info), &res, sizeof(res));

    cprintf("Waiting for display, phys addr for res is %p....\n", (void *)PADDR(&res));
    while (res.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) ;
    cprintf("Display size %dx%d\n", res.pmodes[0].r.width, res.pmodes[0].r.height);
}