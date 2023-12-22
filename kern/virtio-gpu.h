#pragma once

#include <kern/pci.h>
#include "virtio-queue.h"

void init_gpu(struct pci_func *pcif);
int get_display_info();

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

#define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)

struct virtio_gpu_config {
    uint32_t events_read;
    uint32_t events_clear;
    uint32_t num_scanouts;
    uint32_t num_capsets;
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

struct virtio_gpu_device_t {
    struct virtq controlq;
    struct virtq cursorq;

    volatile uint8_t *isr_status;
    volatile struct virtio_gpu_config *conf;

    uint32_t *backbuf;
    size_t backbuf_sz;

    uint32_t screen_w;
    uint32_t screen_h;
};
