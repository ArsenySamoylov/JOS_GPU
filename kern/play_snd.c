#include <kern/play_snd.h>
#include <inc/stdio.h>
#include <kern/hda.h>
#include <kern/env.h>

_Alignas(128) uint8_t buff1[1024] = {};
_Alignas(128) uint8_t buff2[1024] = {};

extern char __rawdataaudio_start[];
extern char __rawdataaudio_end[];

static ssize_t wav_find_data(char *wav, ssize_t wav_sz);

void
play_snd_test() {
    struct hda_stream stream = {};
    stream.dev = hda_get_device();
    /* 48000/16/2 */
    stream.stream_format = (struct hda_fmt){
            .sample_base_rate = 0,
            .bits_per_sample = 1,
            .channel_count = 1};
    stream.buff_size = 1024;
    stream.buff[0] = buff1;
    stream.buff[1] = buff2;
    ssize_t wav_file_sz = (uintptr_t)__rawdataaudio_end - (uintptr_t)__rawdataaudio_start;
    ssize_t wav_data_off = wav_find_data(__rawdataaudio_start, wav_file_sz);
    if (wav_data_off < 0 || wav_file_sz - wav_data_off < 4) {
        cprintf("Unsupported format\n");
        return;
    }
    int16_t *wav_dat = (int16_t *)(__rawdataaudio_start + wav_data_off + 4);
    uint64_t wav_sz = (*(uint32_t *)(__rawdataaudio_start + wav_data_off));
    if (wav_file_sz - (wav_data_off + 4) < wav_sz) {
        cprintf("Unsupported format\n");
        return;
    }
    uint64_t sample_count = wav_sz / 2;
    uint64_t pos = 0;
    for (int i = 0; i < 512; i++) {
        if (pos + i < sample_count) {
            ((volatile int16_t *)stream.buff[0])[i] = wav_dat[pos + i];
        } else {
            ((volatile int16_t *)stream.buff[0])[i] = 0;
        }
    }
    pos += 512;
    for (int i = 0; i < 512; i++) {
        if (pos + i < sample_count) {
            ((volatile int16_t *)stream.buff[1])[i] = wav_dat[pos + i];
        } else {
            ((volatile int16_t *)stream.buff[1])[i] = 0;
        }
    }
    pos += 512;
    int res = hda_stream_init(&stream);
    if (res < 0) {
        cprintf("Couldn't create an audio stream\n");
        return;
    }
    hda_stream_play(&stream);
    while (true) {
        sem_wait(stream.sem);
        int stream_pos = hda_stream_pos(&stream);
        if (pos - 512 + (stream_pos % 1024) / 2 > sample_count) {
            break;
        }
        int curr_buff = stream_pos < 1024;
        for (int i = 0; i < 512; i++) {
            if (pos + i < sample_count) {
                ((volatile int16_t *)stream.buff[curr_buff])[i] = wav_dat[pos + i];
            } else {
                ((volatile int16_t *)stream.buff[curr_buff])[i] = 0;
            }
        }
        pos += 512;
    }
    hda_stream_deinit(&stream);
}

static ssize_t
wav_find_data(char *wav, ssize_t wav_sz) {
    if (wav_sz < 4) {
        return -1;
    }
    const char data_str[] = "data";
    ssize_t i = 0;
    for (; i < wav_sz - 4; i++) {
        if (!__builtin_memcmp(&wav[i], data_str, 4)) {
            return i + 4;
        }
    }
    return -1;
}
