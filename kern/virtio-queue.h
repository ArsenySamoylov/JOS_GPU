#ifndef VIRTQUEUE_H
#define VIRTQUEUE_H

/*
 * An interface for efficient virtio implementation.
 */
#include <inc/types.h>

#define CONTROL_VIRTQ 0
#define CURSOR_VIRTQ  1

#define VIRTQ_SIZE 64
/* This marks a buffer as continuing via the next field. */
#define VIRTQ_DESC_F_NEXT 1
/* This marks a buffer as write-only (otherwise read-only). */
#define VIRTQ_DESC_F_WRITE 2
/* This means the buffer contains a list of buffer descriptors. */
#define VIRTQ_DESC_F_INDIRECT 4
/* The bit of the ISR which indicates a device configuration change. */
#define VIRTIO_PCI_ISR_CONFIG 0x2
#define VIRTIO_PCI_ISR_NOTIFY 0x1
#define VIRTIO_PCI_IRQ_CONFIG 0x1
#define DEFAULT_RESOURCE_ID   0x1


/* The device uses this in used->flags to advise the driver: don't kick me
 * when you add a buffer.  It's unreliable, so it's simply an
 * optimization. */
#define VIRTQ_USED_F_NO_NOTIFY 1
/* The driver uses this in avail->flags to advise the device: don't
 * interrupt me when you consume a buffer.  It's unreliable, so it's
 * simply an optimization.  */
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

/* Support for indirect descriptors */
#define VIRTIO_F_INDIRECT_DESC 28

/* Support for avail_event and used_event fields */
#define VIRTIO_F_EVENT_IDX 29

/* Arbitrary descriptor layouts. */
#define VIRTIO_F_ANY_LAYOUT 27

/* Virtqueue descriptors: 16 bytes.
 * These can chain together via "next". */
struct virtq_desc {
    /* Address (guest-physical). */
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_SIZE];
    /* Only if VIRTIO_F_EVENT_IDX: */ /*... and we have it*/
    uint16_t used_event;
};

/* uint32_t is used here for ids for padding reasons. */
struct virtq_used_elem {
    /* Index of start of used descriptor chain. */
    uint32_t id;
    /* Total length of the descriptor chain which was written to. */
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
    /* Only if VIRTIO_F_EVENT_IDX: */
    uint16_t avail_event;
};

struct virtq {
    uint64_t notify_reg;
    uint32_t log2_size;
    uint32_t used_tail;
    uint32_t desc_first_free;
    uint32_t desc_free_count;
    uint64_t queue_idx;

    _Alignas(4096) struct virtq_desc desc[VIRTQ_SIZE];
    _Alignas(4096) struct virtq_avail avail;
    _Alignas(4096) struct virtq_used used;
};

enum virtio_gpu_ctrl_type {
    /* 2d commands */
    VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
    VIRTIO_GPU_CMD_RESOURCE_UNREF,
    VIRTIO_GPU_CMD_SET_SCANOUT,
    VIRTIO_GPU_CMD_RESOURCE_FLUSH,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
    VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
    VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
    VIRTIO_GPU_CMD_GET_CAPSET_INFO,
    VIRTIO_GPU_CMD_GET_CAPSET,
    VIRTIO_GPU_CMD_GET_EDID,
    VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
    VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

    /* 3d commands */
    VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
    VIRTIO_GPU_CMD_CTX_DESTROY,
    VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
    VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
    VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
    VIRTIO_GPU_CMD_SUBMIT_3D,
    VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
    VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

    /* cursor commands */
    VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
    VIRTIO_GPU_CMD_MOVE_CURSOR,

    /* success responses */
    VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
    VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET,
    VIRTIO_GPU_RESP_OK_EDID,
    VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
    VIRTIO_GPU_RESP_OK_MAP_INFO,

    /* error responses */
    VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
    VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
    VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};


#define VIRTIO_GPU_FLAG_FENCE         (1 << 0)
#define VIRTIO_GPU_FLAG_INFO_RING_IDX (1 << 1)

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t ring_idx;
    uint8_t padding[3];
};

#define VIRTIO_GPU_MAX_SCANOUTS 16

struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one {
        struct virtio_gpu_rect r;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[VIRTIO_GPU_MAX_SCANOUTS];
};

// for VIRTIO_GPU_CMD_RESOURCE_CREATE_2D
enum virtio_gpu_formats {
    VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM = 1,
    VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM = 2,
    VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM = 3,
    VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM = 4,

    VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM = 67,
    VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM = 68,

    VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM = 121,
    VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM = 134,
};

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

// for VIRTIO_GPU_CMD_RESOURCE_FLUSH

struct virtio_gpu_resource_flush { 
        struct virtio_gpu_ctrl_hdr hdr; 
        struct virtio_gpu_rect r; 
        uint32_t resource_id; 
        uint32_t padding; 
};

// for  VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 
struct virtio_gpu_resource_attach_backing { 
        struct virtio_gpu_ctrl_hdr hdr; 
        uint32_t resource_id; 
        uint32_t nr_entries; 
}; 
 
struct virtio_gpu_mem_entry { 
        uint64_t addr; 
        uint32_t length; 
        uint32_t padding; 
};



// for VIRTIO_GPU_CMD_SET_SCANOUT
struct virtio_gpu_set_scanout { 
        struct virtio_gpu_ctrl_hdr hdr; 
        struct virtio_gpu_rect r; 
        uint32_t scanout_id; 
        uint32_t resource_id; 
}; 

// for VIRTIO_GPU_CMD_TRANSFER

struct virtio_gpu_transfer_to_host_2d { 
        struct virtio_gpu_ctrl_hdr hdr; 
        struct virtio_gpu_rect r; 
        uint64_t offset; 
        uint32_t resource_id; 
        uint32_t padding; 
}; 

// for detach backing
struct virtio_gpu_resource_detach_backing { 
        struct virtio_gpu_ctrl_hdr hdr; 
        uint32_t resource_id; 
        uint32_t padding; 
}; 


// for destroy resource
struct virtio_gpu_resource_unref { 
        struct virtio_gpu_ctrl_hdr hdr; 
        uint32_t resource_id; 
        uint32_t padding; 
};

static inline int
virtq_need_event(uint16_t event_idx, uint16_t new_idx, uint16_t old_idx) {
    return (uint16_t)(new_idx - event_idx - 1) < (uint16_t)(new_idx - old_idx);
}

#endif /* VIRTQUEUE_H */
