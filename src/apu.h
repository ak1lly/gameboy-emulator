#ifndef GB_APU_H
#define GB_APU_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GB_APU_RATE 48000u
#define GB_APU_CLOCK 4194304u
#define GB_APU_BUFFER_FRAMES 4096u

typedef struct {
    bool enabled, dac_enabled;
    unsigned length, period, timer, position;
    uint8_t volume, envelope_period, envelope_timer;
    bool envelope_add;
} GB_APU_Channel;

typedef struct {
    uint8_t reg[0x30]; /* FF10-FF3F, including wave RAM */
    bool powered;
    GB_APU_Channel ch[4];
    uint16_t frequency[3];
    uint16_t noise_lfsr;
    uint16_t sweep_shadow;
    uint8_t sweep_timer, frame_step;
    bool sweep_enabled, sweep_negated;
    unsigned frame_cycles, sample_phase;
    int16_t samples[GB_APU_BUFFER_FRAMES * 2];
    size_t pending_frames;
} GB_APU;

void gb_apu_init(GB_APU *a, bool start_after_boot);
uint8_t gb_apu_read(const GB_APU *a, uint16_t address);
void gb_apu_write(GB_APU *a, uint16_t address, uint8_t value);
void gb_apu_tick(GB_APU *a, unsigned tcycles);
/* Interleaved signed 16-bit stereo at GB_APU_RATE. Pops up to frames. */
size_t gb_apu_take_samples(GB_APU *a, int16_t *dst, size_t frames);
void gb_apu_clear_samples(GB_APU *a);
#endif
