#pragma GCC diagnostic warning "-Wall"
#pragma GCC diagnostic warning "-Wextra"

#include <kern/hda.h>
#include <inc/stdio.h>
#include <kern/pci.bits.h>
#include <kern/pmap.h>
#include <kern/sched.h>
#include <kern/trap.h>
/* -_- */
#include <kern/graphic.h>

/* genius design */
void map_addr_early_boot(uintptr_t va, uintptr_t pa, size_t sz);

static bool is_bar_mmio(uint32_t *base_addrs, size_t bar);
static bool is_bar_64bit(uint32_t *base_addrs, size_t bar);
static uint64_t get_bar(uint32_t *base_addrs, size_t bar);

/* malloc... */
static uint8_t mem[0x4000 * 2048] = {};

struct __attribute__((__packed__)) hda_reg {
    uint16_t global_capabilities;
    uint8_t minor_ver;
    uint8_t major_ver;
    uint16_t output_payload_cap;
    uint16_t input_payload_cap;
    uint32_t global_control;
    uint16_t wake_enable;
    uint16_t state_change_status;
    uint16_t global_status;
    uint16_t reserved1[3];
    uint16_t out_strm_payload_capability;
    uint16_t in_strm_payload_capability;
    uint32_t reserved2;
    uint32_t interrupt_control;
    uint32_t interrupt_status;
    uint64_t reserved3;
    uint32_t wall_clock;
    uint32_t stream_sync;
    uint32_t stream_sync_spec;
    uint32_t reserved4;
    uint64_t corb_bar;
    uint16_t corb_write_ptr;
    uint16_t corb_read_ptr;
    uint8_t corb_control;
    uint8_t corb_status;
    uint8_t corb_size;
    uint8_t reserved5;
    uint64_t rirb_bar;
    uint16_t rirb_write_ptr;
    uint16_t response_interrupt_count;
    uint8_t rirb_control;
    uint8_t rirb_status;
    uint8_t rirb_size;
    uint8_t reserved6;
    uint32_t immediate_out;
    uint32_t immediate_in;
    uint32_t immediate_status;
    uint16_t reserved7[2];
    uint64_t dma_bar;
    uint64_t reserved8;
};

struct __attribute__((__packed__)) hda_strm_descr {
    uint32_t control_and_status;
    uint32_t link_position;
    uint32_t buff_len;
    uint16_t last_valid_index;
    uint16_t reserved1;
    uint16_t fifo_size;
    uint16_t format;
    uint32_t reserved2;
    uint64_t buff_descr_list_ptr;
};

struct __attribute__((__packed__)) hda_buffer_descriptor {
    uint64_t addr;
    uint32_t len;
    uint32_t ioc : 1;
    uint32_t : 31;
};

struct __attribute__((__packed__)) hda_command {
    uint32_t data : 8;
    uint32_t command : 12;
    uint32_t node_id : 8;
    uint32_t codec_addr : 4;
};

struct __attribute__((__packed__)) hda_command16 {
    uint32_t data : 16;
    uint32_t command : 4;
    uint32_t node_id : 8;
    uint32_t codec_addr : 4;
};

struct hda_device {
    bool exists;
    volatile struct hda_reg *regs;
    size_t out_strm_count;
    size_t in_strm_count;
    size_t inout_strm_count;
    bool use_corb_rirb;
    volatile uint32_t *corb;
    volatile uint64_t *rirb;
    int corb_write_ptr;
    int rirb_read_ptr;
    volatile struct hda_strm_descr *out_strm;
    int codec_addr;
    int out_node;
    int pin_node;
    bool stream_exists;
    struct sem sem;
    _Alignas(128) struct hda_buffer_descriptor buff_descs[2];
};

static struct hda_device hda_device = {};

static void hda_handle_interrupt();

int send_command(struct hda_device *dev, struct hda_command verb);
uint32_t send_command_im(struct hda_device *dev, struct hda_command verb);
void wait_rirb(struct hda_device *dev);

void
init_hda(struct pci_func *pcif) {
    if (hda_device.exists) { return; }
    uint32_t base_addrs[6] = {};
    for (int i = 0; i < 6; i++) {
        base_addrs[i] = pci_conf_read(pcif, 0x10 + i * sizeof(uint32_t));
    }
    uintptr_t addr = get_bar(base_addrs, 0);
    if (is_bar_mmio(base_addrs, 0)) {
        map_addr_early_boot(addr, addr, HUGE_PAGE_SIZE);
    }
    hda_device.regs = (volatile struct hda_reg *)addr;
    hda_device.regs->global_control &= ~1U;
    while ((hda_device.regs->global_control & 1)) {}
    hda_device.regs->global_control |= 1;
    while (!(hda_device.regs->global_control & 1)) {}
    sleep(10);
    hda_device.regs->wake_enable = 0b0111111111111111;
    hda_device.regs->interrupt_control = 0;
    hda_device.regs->dma_bar = 0;
    hda_device.regs->stream_sync = 0;
    hda_device.regs->stream_sync_spec = 0;
    hda_device.out_strm_count = hda_device.regs->global_capabilities >> 12;
    hda_device.in_strm_count =
            (hda_device.regs->global_capabilities >> 8) & 0xF;
    hda_device.inout_strm_count =
            (hda_device.regs->global_capabilities >> 4) & 0xF;
    uint16_t codec_map = hda_device.regs->state_change_status;
    bool codec_found = false;
    for (; hda_device.codec_addr < 15; hda_device.codec_addr++) {
        if (codec_map & 1) {
            codec_found = true;
            break;
        }
        codec_map >>= 1;
    }
    if (!codec_found) {
        return;
    }
    const uintptr_t mem_addr = (uintptr_t)&mem;
    const uintptr_t corb_addr = (mem_addr & ~(uintptr_t)0x7F) + 0x80;
    const uintptr_t rirb_addr = ((corb_addr + 0x400) & ~(uintptr_t)0x7F) + 0x80;
    hda_device.regs->corb_control &= ~0b10;
    while ((hda_device.regs->corb_control & 0b10)) {}
    hda_device.regs->rirb_control &= ~0b10;
    while ((hda_device.regs->rirb_control & 0b10)) {}
    hda_device.regs->corb_bar = PADDR((void *)corb_addr);
    hda_device.regs->corb_size = 0b10;
    hda_device.regs->corb_read_ptr = 1 << 15;
    while (!(hda_device.regs->corb_read_ptr & (1 << 15))) {}
    hda_device.regs->corb_read_ptr = 0;
    while (hda_device.regs->corb_read_ptr & (1 << 15)) {}
    hda_device.regs->corb_write_ptr = 0;
    hda_device.corb_write_ptr = 0;
    hda_device.regs->rirb_bar = PADDR((void *)rirb_addr);
    hda_device.regs->rirb_size = 0b10;
    hda_device.regs->rirb_write_ptr = 1 << 15;
    sleep(10);
    hda_device.regs->response_interrupt_count = 0;
    sleep(10);
    hda_device.regs->corb_control |= 0b10;
    hda_device.regs->corb_control &= 0xFA;
    while (!(hda_device.regs->corb_control & 0b10)) {}
    hda_device.regs->rirb_control |= 0b10;
    hda_device.regs->corb_control &= 0xFA;
    while (!(hda_device.regs->rirb_control & 0b10)) {}
    hda_device.corb = (volatile uint32_t *)corb_addr;
    hda_device.rirb = (volatile uint64_t *)rirb_addr;
    hda_device.use_corb_rirb = false;
    hda_device.corb[(hda_device.corb_write_ptr + 1) & 0xFF] = *(uint32_t *)&(
            struct hda_command){.codec_addr = hda_device.codec_addr,
                                .command = 0xf00,
                                .node_id = 0,
                                .data = 0};
    hda_device.regs->corb_write_ptr = (hda_device.corb_write_ptr + 1) & 0xFF;
    hda_device.corb_write_ptr = (hda_device.corb_write_ptr + 1) & 0xFF;
    for (int t = 0; t < 20; t++) {
        if ((hda_device.regs->corb_read_ptr & 0xFF) ==
            hda_device.corb_write_ptr) {
            hda_device.use_corb_rirb = true;
            break;
        }
        sleep(1);
    }
    if (!hda_device.use_corb_rirb) {
        hda_device.regs->corb_control &= ~0b10;
        while ((hda_device.regs->corb_control & 0b10)) {}
        hda_device.regs->rirb_control &= ~0b10;
        while ((hda_device.regs->rirb_control & 0b10)) {}
    }
    uint32_t func_groups = send_command_im(
            &hda_device,
            (struct hda_command){.codec_addr = hda_device.codec_addr,
                                 .command = 0xf00,
                                 .node_id = 0,
                                 .data = 4});
    int func_group_count = func_groups & 0xFF;
    int func_group_start_nid = func_groups >> 16;
    uint32_t audio_func_group = 0;
    bool afg_found = false;
    for (int i = func_group_start_nid;
         i < func_group_count + func_group_start_nid; i++) {
        uint32_t func_group_type = send_command_im(
                &hda_device,
                (struct hda_command){.codec_addr = hda_device.codec_addr,
                                     .command = 0xf00,
                                     .node_id = i,
                                     .data = 5});
        if (func_group_type == 1) {
            audio_func_group = i;
            afg_found = true;
            break;
        }
    }
    if (!afg_found) {
        return;
    }
    uint32_t afg_nodes = send_command_im(
            &hda_device,
            (struct hda_command){.codec_addr = hda_device.codec_addr,
                                 .command = 0xf00,
                                 .node_id = audio_func_group,
                                 .data = 4});
    int afg_node_count = afg_nodes & 0xFF;
    int afg_node_start_nid = afg_nodes >> 16;
    hda_device.pin_node = -1;
    hda_device.out_node = -1;
    for (int i = afg_node_start_nid; i < afg_node_count + afg_node_start_nid;
         i++) {
        uint32_t cap = send_command_im(
                &hda_device,
                (struct hda_command){.codec_addr = hda_device.codec_addr,
                                     .command = 0xf00,
                                     .node_id = i,
                                     .data = 9});
        if (cap >> 20 == 0 && hda_device.out_node == -1) {
            hda_device.out_node = i;
        } else if (cap >> 20 == 4 && hda_device.pin_node == -1) {
            uint32_t pin_cap = send_command_im(
                    &hda_device,
                    (struct hda_command){.codec_addr = hda_device.codec_addr,
                                         .command = 0xf00,
                                         .node_id = i,
                                         .data = 0xC});
            if (pin_cap & 0x10) { /* is output-capable */
                hda_device.pin_node = i;
            }
        }
    }
    if (hda_device.pin_node == -1 || hda_device.out_node == -1) {
        return;
    }
    uint32_t conn_list_len = send_command_im(
            &hda_device,
            (struct hda_command){.codec_addr = hda_device.codec_addr,
                                 .command = 0xf00,
                                 .node_id = hda_device.pin_node,
                                 .data = 0xE});
    if (conn_list_len != 1) {
        return;
    }
    send_command_im(&hda_device,
                    (struct hda_command){.codec_addr = hda_device.codec_addr,
                                         .node_id = hda_device.pin_node,
                                         .command = 0x701,
                                         .data = 0});
    hda_device.out_strm =
            (volatile struct hda_strm_descr *)(addr + sizeof(struct hda_reg) +
                                               sizeof(struct hda_strm_descr) *
                                                       hda_device
                                                               .in_strm_count);
    reg_irq(pcif->irq_line, &hda_handle_interrupt);
    hda_device.regs->interrupt_control |= (1 << 31);
    hda_device.regs->interrupt_control |= (1 << hda_device.in_strm_count);
    hda_device.exists = true;
}

int
send_command(struct hda_device *dev, struct hda_command verb) {
    if (!dev->use_corb_rirb) { return -2; }
    while (((dev->corb_write_ptr + 1) & 0xFF) ==
           (dev->regs->corb_read_ptr & 0xFF)) {}
    dev->corb[(dev->corb_write_ptr + 1) & 0xFF] = *(uint32_t *)&verb;
    dev->regs->corb_write_ptr = (dev->corb_write_ptr + 1) & 0xFF;
    dev->corb_write_ptr = (dev->corb_write_ptr + 1) & 0xFF;
    return 0;
}

uint32_t
send_command_im(struct hda_device *dev, struct hda_command verb) {
    if (dev->use_corb_rirb) {
        send_command(dev, verb);
        wait_rirb(dev);
        dev->rirb_read_ptr = (dev->rirb_read_ptr + 1) & 0xFF;
        return dev->rirb[dev->rirb_read_ptr];
    }
    dev->regs->immediate_status |= 0b10;
    dev->regs->immediate_out = *(uint32_t *)&verb;
    dev->regs->immediate_status |= 1;
    for (int t = 0; t < 40; t++) {
        if ((dev->regs->immediate_status & 0b11) == 0b10) {
            uint32_t ret = dev->regs->immediate_in;
            dev->regs->immediate_status = 0b10;
            return ret;
        }
        sleep(1);
    }
    sleep(10);
    return 0;
}

void
wait_rirb(struct hda_device *dev) {
    while ((dev->regs->rirb_write_ptr & 0xFF) == dev->rirb_read_ptr) {}
}

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

struct hda_device *
hda_get_device(void) {
    if (!hda_device.exists) { return NULL; }
    return &hda_device;
}

int
hda_stream_init(struct hda_stream *strm) {
    if (strm->dev->stream_exists) { return -1; }
    asm volatile("cli");
    strm->dev->buff_descs[0].addr = PADDR((void *)strm->buff[0]);
    strm->dev->buff_descs[0].ioc = 1;
    strm->dev->buff_descs[0].len = strm->buff_size;
    strm->dev->buff_descs[1].addr = PADDR((void *)strm->buff[1]);
    strm->dev->buff_descs[1].ioc = 1;
    strm->dev->buff_descs[1].len = strm->buff_size;
    strm->dev->out_strm->control_and_status |= 1;
    while (!(strm->dev->out_strm->control_and_status & 1)) {}
    strm->dev->out_strm->control_and_status &= ~1;
    while (strm->dev->out_strm->control_and_status & 1) {}
    strm->dev->out_strm->control_and_status |= 1 << 20; /* stream num = 1 */
    strm->dev->out_strm->format = *(uint16_t *)&strm->stream_format;
    strm->dev->out_strm->control_and_status |= 1 << 2;
    strm->dev->out_strm->last_valid_index = 1;
    strm->dev->out_strm->buff_descr_list_ptr = PADDR(strm->dev->buff_descs);
    strm->dev->out_strm->buff_len = strm->buff_size * 2;
    uint32_t out_pw_res = send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0xf05,
                                            .node_id = strm->dev->out_node,
                                            .data = 0});
    send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0x705,
                                            .node_id = strm->dev->out_node,
                                            .data = 0});
    sleep(100);
    out_pw_res = send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0xf05,
                                            .node_id = strm->dev->out_node,
                                            .data = 0});
    uint32_t pin_pw_res = send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0xf05,
                                            .node_id = strm->dev->pin_node,
                                            .data = 0});
    send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0x705,
                                            .node_id = strm->dev->pin_node,
                                            .data = 0});
    sleep(100);
    pin_pw_res = send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0xf05,
                                            .node_id = strm->dev->pin_node,
                                            .data = 0});
    send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0x707,
                                            .node_id = strm->dev->pin_node,
                                            .data = (1 << 7) | (1 << 6)});
    send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0x706,
                                            .node_id = strm->dev->out_node,
                                            .data = 1 << 4});

    uint32_t out_amp_capabilities = send_command_im(
            strm->dev, (struct hda_command){.codec_addr = strm->dev->codec_addr,
                                            .command = 0xF00,
                                            .node_id = strm->dev->out_node,
                                            .data = 0x12});
    int steps = (out_amp_capabilities >> 8) & 127;
    uint16_t set_amp_payload = (1 << 15) | (1 << 13) | (1 << 12) | steps;
    send_command_im(strm->dev, *(struct hda_command *)&(struct hda_command16){
                                       .codec_addr = strm->dev->codec_addr,
                                       .command = 0x3,
                                       .node_id = strm->dev->out_node,
                                       .data = set_amp_payload});
    strm->sem = &strm->dev->sem;
    sleep(10);
    strm->dev->stream_exists = true;
    asm volatile("sti");
    return 0;
}

void
hda_stream_deinit(struct hda_stream *strm) {
    hda_stream_pause(strm);
    strm->dev->stream_exists = false;
}

int
hda_stream_play(struct hda_stream *strm) {
    asm volatile("cli");
    strm->dev->out_strm->control_and_status |= 0b10;
    while (!(strm->dev->out_strm->control_and_status & 0b10)) {}
    strm->dev->regs->stream_sync |= 1;
    strm->dev->regs->stream_sync_spec |= 1;
    asm volatile("sti");
    return 0;
}

int
hda_stream_pos(struct hda_stream *strm) {
    return strm->dev->out_strm->link_position;
}

int
hda_stream_pause(struct hda_stream *strm) {
    asm volatile("cli");
    strm->dev->out_strm->control_and_status &= ~0b10;
    while (strm->dev->out_strm->control_and_status & 0b10) {}
    asm volatile("sti");
    return 0;
}

static void
hda_handle_interrupt() {
    if (!(hda_device.regs->interrupt_status &
          (1 << hda_device.in_strm_count)) || 
        !(hda_device.out_strm->control_and_status & (1 << 26))) {
        return;
    }
    hda_device.out_strm->control_and_status &= ~((uint32_t)(1 << 26));
    if (hda_device.stream_exists && hda_device.sem.val == 0) {
        hda_device.sem.val += 1;
    }
}
