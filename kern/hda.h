#ifndef JOS_INC_HDA_H
#define JOS_INC_HDA_H

#include <kern/pci.h>
#include <kern/spinlock.h>
#include <kern/sem.h>

struct __attribute__((__packed__)) hda_fmt {
    uint16_t channel_count : 4;
    uint16_t bits_per_sample : 3;
    uint16_t : 1;
    uint16_t sample_base_rate_divisor : 3;
    uint16_t sample_base_rate_multiplier : 3;
    uint16_t sample_base_rate : 1;
    uint16_t stream_type : 1;
};

struct hda_device;
struct hda_stream {
    struct hda_device *dev;
    struct sem *sem;
    struct hda_fmt stream_format;
    size_t buff_size;
    volatile uint8_t *buff[2];
    struct spinlock lock;
};

void init_hda(struct pci_func *pfnc);

struct hda_device *hda_get_device(void);

int hda_stream_init(struct hda_stream *strm);
void hda_stream_deinit(struct hda_stream *strm);
int hda_stream_play(struct hda_stream *strm);
int hda_stream_pos(struct hda_stream *strm);
int hda_stream_pause(struct hda_stream *strm);

#endif /* JOS_INC_HDA_H */
