#include "apu.h"
#include <string.h>

static const uint8_t read_masks[0x17] = {
    0x80,0x3F,0x00,0xFF,0xBF,0xFF,0x3F,0x00,0xFF,0xBF,0x7F,
    0xFF,0x9F,0xFF,0xBF,0xFF,0xFF,0x00,0x00,0xBF,0x00,0x00,0x70
};
static const uint8_t duty[4] = {0x01,0x81,0x87,0x7E};
static const unsigned noise_divisor[8] = {8,16,32,48,64,80,96,112};

static unsigned period_square(uint16_t f) { return (2048u - (f & 2047u)) * 4u; }
static unsigned period_wave(uint16_t f) { return (2048u - (f & 2047u)) * 2u; }
static unsigned period_noise(const GB_APU *a) {
    uint8_t nr43 = a->reg[0x12];
    return noise_divisor[nr43 & 7] << (nr43 >> 4);
}
static bool length_enabled(const GB_APU *a, unsigned c) {
    static const unsigned idx[4] = {4,9,14,19};
    return (a->reg[idx[c]] & 0x40u) != 0;
}
static void update_frequency(GB_APU *a, unsigned c) {
    static const unsigned lo[3] = {3,8,13};
    a->frequency[c] = (uint16_t)(a->reg[lo[c]] | ((a->reg[lo[c]+1] & 7u) << 8));
    a->ch[c].period = c == 2 ? period_wave(a->frequency[c]) : period_square(a->frequency[c]);
}
static void trigger(GB_APU *a, unsigned c) {
    GB_APU_Channel *v = &a->ch[c];
    if (!v->length) v->length = c == 2 ? 256u : 64u;
    v->enabled = v->dac_enabled;
    v->position = 0;
    if (c == 0 || c == 1) {
        update_frequency(a, c);
        uint8_t env = a->reg[c == 0 ? 2 : 7];
        v->volume = env >> 4;
        v->envelope_period = env & 7;
        v->envelope_timer = v->envelope_period ? v->envelope_period : 8;
        v->envelope_add = (env & 8) != 0;
    } else if (c == 2) {
        update_frequency(a, c);
    } else {
        uint8_t env = a->reg[0x11];
        v->volume = env >> 4;
        v->envelope_period = env & 7;
        v->envelope_timer = v->envelope_period ? v->envelope_period : 8;
        v->envelope_add = (env & 8) != 0;
        v->period = period_noise(a);
        a->noise_lfsr = 0x7FFF;
    }
    v->timer = v->period;
    if (c == 0) {
        uint8_t nr10 = a->reg[0];
        a->sweep_shadow = a->frequency[0];
        a->sweep_timer = (nr10 >> 4) & 7u;
        if (!a->sweep_timer) a->sweep_timer = 8;
        a->sweep_enabled = ((nr10 & 0x70) != 0) || ((nr10 & 7) != 0);
        a->sweep_negated = false;
    }
}
static unsigned sweep_next(GB_APU *a) {
    unsigned shift = a->reg[0] & 7;
    unsigned delta = a->sweep_shadow >> shift;
    if (a->reg[0] & 8) { a->sweep_negated = true; return a->sweep_shadow - delta; }
    return a->sweep_shadow + delta;
}
static void frame_clock(GB_APU *a) {
    unsigned s = a->frame_step;
    if ((s & 1) == 0) {
        for (unsigned c = 0; c < 4; ++c)
            if (length_enabled(a, c) && a->ch[c].length && --a->ch[c].length == 0)
                a->ch[c].enabled = false;
    }
    if (s == 2 || s == 6) {
        uint8_t nr10 = a->reg[0];
        if (a->sweep_timer && --a->sweep_timer == 0) {
            a->sweep_timer = (nr10 >> 4) & 7;
            if (!a->sweep_timer) a->sweep_timer = 8;
            if (a->sweep_enabled && (nr10 & 0x70)) {
                unsigned next = sweep_next(a);
                if (next > 2047) a->ch[0].enabled = false;
                else if (nr10 & 7) {
                    a->sweep_shadow = (uint16_t)next;
                    a->frequency[0] = (uint16_t)next;
                    a->reg[3] = (uint8_t)next;
                    a->reg[4] = (uint8_t)((a->reg[4] & ~7u) | (next >> 8));
                    a->ch[0].period = period_square((uint16_t)next);
                    if (sweep_next(a) > 2047) a->ch[0].enabled = false;
                }
            }
        }
    }
    if (s == 7) {
        for (unsigned c = 0; c < 4; ++c) {
            if (c == 2) continue;
            GB_APU_Channel *v = &a->ch[c];
            if (v->envelope_period && v->envelope_timer && --v->envelope_timer == 0) {
                v->envelope_timer = v->envelope_period;
                if (v->envelope_add && v->volume < 15) ++v->volume;
                else if (!v->envelope_add && v->volume > 0) --v->volume;
            }
        }
    }
    a->frame_step = (uint8_t)((s + 1) & 7);
}
static void advance_periods(GB_APU *a) {
    for (unsigned c = 0; c < 4; ++c) {
        GB_APU_Channel *v = &a->ch[c];
        if (!v->timer) v->timer = v->period ? v->period : 8;
        if (--v->timer == 0) {
            v->timer = v->period ? v->period : 8;
            if (c == 3) {
                unsigned xor_bit = (a->noise_lfsr ^ (a->noise_lfsr >> 1)) & 1;
                a->noise_lfsr = (uint16_t)((a->noise_lfsr >> 1) | (xor_bit << 14));
                if (a->reg[0x12] & 8)
                    a->noise_lfsr = (uint16_t)((a->noise_lfsr & ~(1u << 6)) | (xor_bit << 6));
            } else v->position = (v->position + 1) & (c == 2 ? 31u : 7u);
        }
    }
}
static int channel_output(const GB_APU *a, unsigned c) {
    const GB_APU_Channel *v = &a->ch[c];
    if (!a->powered || !v->enabled || !v->dac_enabled) return 0;
    if (c == 2) {
        uint8_t level = (a->reg[0x0C] >> 5) & 3;
        if (!level) return 0;
        uint8_t packed = a->reg[0x20 + v->position / 2];
        int nibble = (v->position & 1) ? (packed & 15) : (packed >> 4);
        return (nibble >> (level - 1)) - (15 >> (level - 1)) / 2;
    }
    if (c == 3) return (a->noise_lfsr & 1) ? -(int)v->volume : (int)v->volume;
    return (duty[(a->reg[c == 0 ? 1 : 6] >> 6) & 3] & (1u << v->position)) ? (int)v->volume : -(int)v->volume;
}
static void emit_sample(GB_APU *a) {
    if (a->pending_frames == GB_APU_BUFFER_FRAMES) return; /* bounded: drop samples without an output consumer */
    int left = 0, right = 0;
    for (unsigned c = 0; c < 4; ++c) {
        int value = channel_output(a, c);
        if (a->reg[0x15] & (1u << (c + 4))) left += value;
        if (a->reg[0x15] & (1u << c)) right += value;
    }
    left *= (int)((a->reg[0x14] >> 4 & 7) + 1);
    right *= (int)((a->reg[0x14] & 7) + 1);
    /* Keep 4 simultaneous channels within signed 16-bit range. */
    size_t at = a->pending_frames++ * 2;
    a->samples[at] = (int16_t)(left * 32);
    a->samples[at + 1] = (int16_t)(right * 32);
}
void gb_apu_init(GB_APU *a, bool post_boot) {
    memset(a, 0, sizeof(*a));
    a->noise_lfsr = 0x7FFF;
    if (post_boot) {
        a->powered = true;
        static const uint8_t initial[0x17] = {
            0x80,0xBF,0xF3,0xFF,0xBF,0xFF,0x3F,0x00,0xFF,0xBF,0x7F,
            0xFF,0x9F,0xFF,0xBF,0xFF,0xFF,0xFF,0x00,0xBF,0x77,0xF3,0x80
        };
        memcpy(a->reg, initial, sizeof(initial));
        for (unsigned c = 0; c < 3; ++c) update_frequency(a, c);
        a->ch[0].dac_enabled = true;
        a->ch[2].dac_enabled = false;
        /* Post-boot state can report CH1 on, but audio is quiet until triggered. */
    }
}
uint8_t gb_apu_read(const GB_APU *a, uint16_t address) {
    unsigned idx = address - 0xFF10u;
    if (address == 0xFF26) {
        unsigned status = a->powered ? 0x80u : 0u;
        for (unsigned c = 0; c < 4; ++c) if (a->ch[c].enabled) status |= 1u << c;
        return (uint8_t)(0x70u | status);
    }
    if (idx >= 0x20 && idx <= 0x2F) return a->reg[idx];
    if (idx >= 0x17) return 0xFF;
    return (uint8_t)(a->reg[idx] | read_masks[idx]);
}
void gb_apu_write(GB_APU *a, uint16_t address, uint8_t value) {
    unsigned i = address - 0xFF10u;
    if (i >= 0x20 && i <= 0x2F) { a->reg[i] = value; return; }
    if (address == 0xFF26) {
        if ((value & 0x80) == 0) {
            memset(a->reg, 0, 0x17);
            memset(a->ch, 0, sizeof(a->ch));
            a->powered = false;
            a->frame_step = 0;
            a->sweep_enabled = false;
        } else if (!a->powered) { a->powered = true; a->frame_step = 0; }
        return;
    }
    if (!a->powered || i >= 0x17) return;
    a->reg[i] = value;
    switch (address) {
        case 0xFF10:
            if (a->sweep_negated && !(value & 8)) a->ch[0].enabled = false;
            break;
        case 0xFF11: a->ch[0].length = 64 - (value & 63); break;
        case 0xFF16: a->ch[1].length = 64 - (value & 63); break;
        case 0xFF1B: a->ch[2].length = 256 - value; break;
        case 0xFF20: a->ch[3].length = 64 - (value & 63); break;
        case 0xFF12: a->ch[0].dac_enabled = (value & 0xF8) != 0;
                     if (!a->ch[0].dac_enabled) a->ch[0].enabled = false;
                     break;
        case 0xFF17: a->ch[1].dac_enabled = (value & 0xF8) != 0;
                     if (!a->ch[1].dac_enabled) a->ch[1].enabled = false;
                     break;
        case 0xFF1A: a->ch[2].dac_enabled = (value & 0x80) != 0;
                     if (!a->ch[2].dac_enabled) a->ch[2].enabled = false;
                     break;
        case 0xFF21: a->ch[3].dac_enabled = (value & 0xF8) != 0;
                     if (!a->ch[3].dac_enabled) a->ch[3].enabled = false;
                     break;
        case 0xFF13: case 0xFF14: update_frequency(a, 0); break;
        case 0xFF18: case 0xFF19: update_frequency(a, 1); break;
        case 0xFF1D: case 0xFF1E: update_frequency(a, 2); break;
        case 0xFF22: a->ch[3].period = period_noise(a); break;
        default: break;
    }
    if ((address == 0xFF14 || address == 0xFF19 || address == 0xFF1E || address == 0xFF23) && (value & 0x80)) {
        unsigned c = address == 0xFF14 ? 0 : address == 0xFF19 ? 1 : address == 0xFF1E ? 2 : 3;
        trigger(a, c);
    }
}
void gb_apu_tick(GB_APU *a, unsigned tcycles) {
    for (unsigned t = 0; t < tcycles; ++t) {
        if (a->powered) {
            advance_periods(a);
            if (++a->frame_cycles == 8192) { a->frame_cycles = 0; frame_clock(a); }
        }
        a->sample_phase += GB_APU_RATE;
        if (a->sample_phase >= GB_APU_CLOCK) {
            a->sample_phase -= GB_APU_CLOCK;
            emit_sample(a);
        }
    }
}
size_t gb_apu_take_samples(GB_APU *a, int16_t *dst, size_t frames) {
    size_t n = frames < a->pending_frames ? frames : a->pending_frames;
    if (!n) return 0;
    memcpy(dst, a->samples, n * 2 * sizeof(int16_t));
    a->pending_frames -= n;
    memmove(a->samples, a->samples + n*2, a->pending_frames * 2 * sizeof(int16_t));
    return n;
}
void gb_apu_clear_samples(GB_APU *a) { a->pending_frames = 0; }
