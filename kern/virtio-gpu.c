#include "virtio-gpu.h"
#include "virtio-queue.h"
#include <inc/stdio.h>
#include <kern/pcireg.h>
#include <kern/pmap.h>
#include <kern/pci.bits.h>
#include <inc/string.h>
#include "graphic.h"

struct virtio_gpu_device_t gpu;

void
map_addr_early_boot(uintptr_t va, uintptr_t pa, size_t sz);

static void setup_queue(struct virtq *queue, volatile struct virtio_pci_common_cfg_t *cfg_header);
static int test_draw();

// ---------------------------------------------------------------------------------------------------------------------
// defines

#define atomic_ld_acq(value) \
    __atomic_load_n(value, __ATOMIC_ACQUIRE)

#define atomic_st_rel(value, rhs) \
    __atomic_store_n((value), (rhs), __ATOMIC_RELEASE)

#define atomic_fence() __atomic_thread_fence(__ATOMIC_SEQ_CST)

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
    gpu.cursorq.queue_idx = CURSOR_VIRTQ;
    setup_queue(&gpu.cursorq, cfg_header);

    gpu.controlq.queue_idx = CONTROL_VIRTQ;
    setup_queue(&gpu.controlq, cfg_header);

    // Set DRIVER_OK flag
    cfg_header->device_status |= VIRTIO_STATUS_DRIVER_OK;
}

static void
setup_queue(struct virtq *queue, volatile struct virtio_pci_common_cfg_t *cfg_header) {
    cfg_header->queue_select = queue->queue_idx;
    cfg_header->queue_desc   = (uint64_t)PADDR(&queue->desc);
    cfg_header->queue_avail  = (uint64_t)PADDR(&queue->avail);
    cfg_header->queue_used   = (uint64_t)PADDR(&queue->used);
    cfg_header->queue_enable = 1;
    cfg_header->queue_size   = VIRTQ_SIZE;
    queue->log2_size = 6;

    queue->desc_free_count = 1 << queue->log2_size;
    for (int i = queue->desc_free_count; i > 0; --i) {
        queue->desc[i - 1].next = queue->desc_first_free;
        queue->desc_first_free = i - 1;
    }
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
            gpu.controlq.notify_reg += notify_reg;

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

            gpu.controlq.notify_reg += get_bar(base_addrs, cap_header.bar);

            pci_memcpy_from(pcif, cap_offset_old + sizeof(cap_header),
                            (uint8_t *)&notify_off_multiplier, sizeof(notify_off_multiplier));
            break;
        case VIRTIO_PCI_CAP_ISR_CFG:
            addr = cap_header.offset + get_bar(base_addrs, cap_header.bar);
            gpu.isr_status = (uint8_t *)addr;
            break;
        case VIRTIO_PCI_CAP_DEVICE_CFG:
            gpu.conf = (struct virtio_gpu_config *)(cap_header.offset + get_bar(base_addrs, cap_header.bar));
            break;
        case VIRTIO_PCI_CAP_PCI_CFG: break;
        default: break;
        }

        cap_offset = cap_header.cap_next;
    }

    get_display_info();
    test_draw();
}


static void
recycle_used(struct virtq *queue) {
    size_t tail = queue->used_tail;
    size_t const mask = ~-(1 << queue->log2_size);
    size_t const done_idx = atomic_ld_acq(&queue->used.idx);

    do {
        struct virtq_used_elem *used = &queue->used.ring[tail & mask];
        uint16_t id = used->id;

        unsigned freed_count = 1;

        uint16_t end = id;
        while (queue->desc[end].flags & VIRTQ_DESC_F_NEXT) {
            end = queue->desc[end].next;
            ++freed_count;
        }

        queue->desc[end].next = queue->desc_first_free;
        queue->desc_first_free = id;
        queue->desc_free_count += freed_count;
    } while ((++tail & 0xFFFF) != done_idx);

    queue->used_tail = tail;

    // [NOTE]: read 2.7.8 please, driver should not write to used ring at all
    // Да и значение у этого поля совсем другое — тут устройство говорит, до какого буфера его можно не тыкать
    // по аналогии с avail.used_events
    // Notify device how far used ring has been processed
    // atomic_st_rel(&queue->used.avail_event, tail);
}


static void
config_irq() {
    int events = gpu.conf->events_read;

    // clear events
    if (events & VIRTIO_PCI_IRQ_CONFIG) {
        gpu.conf->events_clear = events;
    }
}

static void
notify_queue(struct virtq *queue) {
    *((uint64_t *)queue->notify_reg) = queue->queue_idx;
}

static void
irq_handler() {
    uint8_t isr = *(gpu.isr_status);
    if (isr & VIRTIO_PCI_ISR_CONFIG) {
        config_irq();
    } else if (isr & VIRTIO_PCI_ISR_NOTIFY) {
        cprintf("Recycle descriptors\n");
        recycle_used(&gpu.controlq);
        notify_queue(&gpu.controlq);
    }
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

    atomic_fence();

    // Update idx (tell virtio where we would put the next new item)
    // enforce ordering until after prior store is globally visible
    atomic_st_rel(&queue->avail.idx, avail_head);

    atomic_fence();
}


static struct virtq_desc *
alloc_desc(struct virtq *queue, int writable) {
    if (queue->desc_free_count == 0)
        return NULL;

    --queue->desc_free_count;

    struct virtq_desc *desc = &queue->desc[queue->desc_first_free];
    queue->desc_first_free = desc->next;

    desc->flags = 0;
    desc->next = -1;

    if (writable)
        desc->flags |= VIRTQ_DESC_F_WRITE;

    return desc;
}

static void
send_and_recieve(struct virtq *queue, void *to_send, uint64_t send_size, void *to_recieve, uint64_t recieve_size) {
    struct virtq_desc *desc[2] = { 
        [0] = alloc_desc(queue, 0),
        [1] = alloc_desc(queue, 1)
    };

    desc[0]->addr = (uint64_t)PADDR(to_send);
    desc[0]->len = send_size;
    desc[0]->flags = VIRTQ_DESC_F_NEXT;
    desc[0]->next = 1;

    desc[1]->addr = (uint64_t)PADDR(to_recieve);
    desc[1]->len = recieve_size;

    atomic_fence();

    queue_avail(queue, 2);
    notify_queue(queue);

    while (!((struct virtio_gpu_ctrl_hdr *)to_recieve)->type) {
        asm volatile("pause");
    }
    irq_handler();
}

int
get_display_info() {
    struct virtio_gpu_ctrl_hdr display_info = {.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO};
    struct virtio_gpu_resp_display_info res = {};

    send_and_recieve(&gpu.controlq, &display_info, sizeof(display_info), &res, sizeof(res));

    if (res.hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        gpu.screen_h = res.pmodes[0].r.height;
        gpu.screen_w = res.pmodes[0].r.width;
        cprintf("Display size %dx%d\n", gpu.screen_w, gpu.screen_h);
    }

    return 0;
}

static int
resource_create_2d(struct texture_2d *texture) {
    // Create a host resource using VIRTIO_GPU_CMD_RESOURCE_CREATE_2D.

    struct virtio_gpu_resource_create_2d resource_2d = {
        .hdr.type    = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
        .height      = texture->height,
        .width       = texture->width,
        .format      = VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM,
        .resource_id = texture->resource_id
    };

    struct virtio_gpu_ctrl_hdr res = {};

    // send and recieve information

    send_and_recieve(&gpu.controlq, &resource_2d, sizeof(resource_2d), &res, sizeof(res));

    if (res.type == VIRTIO_GPU_RESP_OK_NODATA) {
        cprintf("Success 2d resource created\n");
    } else {
        return 1;
    }
    return 0;
}


static int
attach_backing(struct surface_t *surface) {
    // Allocate a surface from guest ram, and attach it as backing storage to the resource just created,
    // using VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING. Scatter lists are supported,
    // so the surface doesn’t need to be contignous in guest physical memory.

    struct virtio_gpu_ctrl_hdr res = {};

    // Calculate size of attach backing command
    size_t backing_cmd_sz = sizeof(struct virtio_gpu_resource_attach_backing) +
                            sizeof(struct virtio_gpu_mem_entry);

    // Allocate a buffer to hold virtio_gpu_resource_attach_backing_t command
    struct virtio_gpu_resource_attach_backing backing_cmd[2] = {};
    backing_cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;

    backing_cmd->resource_id = surface->resource_id;
    backing_cmd->nr_entries = 1;

    struct virtio_gpu_mem_entry *mem_entries =
            (struct virtio_gpu_mem_entry *)(backing_cmd + 1);

    mem_entries->addr = (uint64_t)PADDR(surface->backbuf); /*backbuf phys addr*/
    mem_entries->length = surface->width * surface->height * sizeof(uint32_t);

    send_and_recieve(&gpu.controlq, backing_cmd, backing_cmd_sz,
                     &res, sizeof(res));

    if (res.type == VIRTIO_GPU_RESP_OK_NODATA) {
        cprintf("Success attach backing\n");
    } else {
        return 1;
    }
    return 0;
}

static int
detach_backing(uint32_t resource_id) {
    struct virtio_gpu_ctrl_hdr res = {};

    struct virtio_gpu_resource_detach_backing detach_backing = {};
    detach_backing.hdr.type = VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE;
    detach_backing.resource_id = resource_id;

    send_and_recieve(&gpu.controlq, &detach_backing, sizeof(detach_backing),
                        &res, sizeof(res));

    if (res.type == VIRTIO_GPU_RESP_OK_NODATA) {
        cprintf("Success dettach backing\n");
    } else {
        cprintf("Res error %s\n", virtio_strerror(res.type));
    }
    return 0;
}

static int resource_unref(uint32_t resource_id)
{
    struct virtio_gpu_resource_unref unref = {};
    unref.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    struct virtio_gpu_ctrl_hdr res = {};

    unref.resource_id = resource_id;

    send_and_recieve(&gpu.controlq, &unref, sizeof(unref),
                        &res, sizeof(res));


    if (res.type == VIRTIO_GPU_RESP_OK_NODATA) {
        cprintf("Success resource unref\n");
    } else {
        cprintf("Res error %s\n", virtio_strerror(res.type));
    }
    return 0;
}

static int
set_scanout(struct surface_t *surface) {
    // Use VIRTIO_GPU_CMD_SET_SCANOUT to link the surface to a display scanout.

    struct virtio_gpu_set_scanout scanout = {
            .hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT,
            // we don't support (yet) region drawing, only whole surface
            .r.x = surface->pos_x,
            .r.y = surface->pos_y,
            .r.width = surface->width,
            .r.height = surface->height,
            .resource_id = surface->resource_id,
            .scanout_id = 0
    };
    struct virtio_gpu_ctrl_hdr res = {};

    send_and_recieve(&gpu.controlq, &scanout, sizeof(scanout),
                     &res, sizeof(res));

    if (res.type == VIRTIO_GPU_RESP_OK_NODATA) {
        cprintf("Success setting scanout\n");
        return 0;
    } else {
        cprintf("Res type %d\n", res.type);
    }
    return 1;
}

static int
transfer_to_host_2D(struct surface_t *surface) {
    // Use VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D to update the host resource from guest memory.
    struct virtio_gpu_transfer_to_host_2d transfer = {
            .hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
            .r.x = surface->pos_x,
            .r.y = surface->pos_y,
            .r.width = surface->width, // we want to transfer whole buf
            .r.height = surface->height,
            .resource_id = surface->resource_id};
    struct virtio_gpu_ctrl_hdr res = {};

    send_and_recieve(&gpu.controlq, &transfer, sizeof(transfer),
                     &res, sizeof(res));

    if (res.type == VIRTIO_GPU_RESP_OK_NODATA) {
        cprintf("Transfer to host 2D completed\n");
        return 0;
    } else {
        cprintf("Res type %d\n", res.type);
    }
    return 1;
}

static int
flush(struct surface_t *surface) {
    // Use VIRTIO_GPU_CMD_RESOURCE_FLUSH to flush the updated resource to the display.
    struct virtio_gpu_resource_flush flush = {
            .hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH,
            .r.x = surface->pos_x,
            .r.y = surface->pos_y,
            .r.width = surface->width,
            .r.height = surface->height,
            .resource_id = surface->resource_id};

    struct virtio_gpu_ctrl_hdr res = {};

    send_and_recieve(&gpu.controlq, &flush, sizeof(flush),
                     &res, sizeof(res));

    if (res.type == VIRTIO_GPU_RESP_OK_NODATA) {
        cprintf("Flush completed\n");
        return 0;
    } else {
        cprintf("Res type %d\n", res.type);
    }
    return 1;
}

void
surface_init(struct surface_t *surface, uint32_t buf_w, uint32_t buf_h) {
    surface->resource_id = ++gpu.resource_id_cnt; // so we start from 1
    surface->width = buf_w;
    surface->height = buf_h;
    surface->pos_x = 0;
    surface->pos_y = 0;

    resource_create_2d(surface);
    attach_backing(surface);
    set_scanout(surface);
}

void
surface_display(struct surface_t *surface) {
    // update host surface
    transfer_to_host_2D(surface);
    // flush to window
    flush(surface);
}

void
surface_destroy(struct surface_t *surface) {
    detach_backing(surface->resource_id);
    resource_unref(surface->resource_id);
}

struct surface_t resource = {};

int
test_draw() {
    surface_init(&resource, gpu.screen_w, gpu.screen_h);
    struct vector pos = {.x = 50, .y = 50};
    draw_circle(&resource, pos, 50, TEST_XRGB_RED);
    surface_display(&resource);
    return 0;
}