#include "bus.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t rd(GB_Bus *b, unsigned a) { return gb_bus_read(b, (uint16_t)a); }
static void wr(GB_Bus *b, unsigned a, unsigned v) { gb_bus_write(b, (uint16_t)a, (uint8_t)v); }
static void test_power(GB_Bus *b) {
    gb_bus_init(b, false);
    assert((rd(b, 0xFF26) & 0x80) == 0);
    wr(b, 0xFF12, 0xF3);
    assert(rd(b, 0xFF12) == 0);
    wr(b, 0xFF26, 0x80);
    wr(b, 0xFF12, 0xF3);
    assert(rd(b, 0xFF12) == 0xF3);
    wr(b, 0xFF26, 0);
    assert(rd(b, 0xFF12) == 0);
    assert((rd(b, 0xFF26) & 0x80) == 0);
}
static void test_wave(GB_Bus *b) {
    gb_bus_init(b, false);
    wr(b, 0xFF30, 0xAB);
    assert(rd(b, 0xFF30) == 0xAB);
    wr(b, 0xFF26, 0x80);
    wr(b, 0xFF1A, 0x80);
    wr(b, 0xFF1C, 0x20);
    wr(b, 0xFF1E, 0x80);
    assert((rd(b, 0xFF26) & 4) != 0);
    wr(b, 0xFF1A, 0);
    assert((rd(b, 0xFF26) & 4) == 0);
    wr(b, 0xFF26, 0);
    assert(rd(b, 0xFF30) == 0xAB);
}
static void test_square_and_envelope(GB_Bus *b) {
    gb_bus_init(b, false);
    wr(b, 0xFF26, 0x80);
    wr(b, 0xFF24, 0x77);
    wr(b, 0xFF25, 0x11);
    wr(b, 0xFF11, 0x80);
    wr(b, 0xFF12, 0xF1);
    wr(b, 0xFF13, 0);
    wr(b, 0xFF14, 0x80);
    assert(rd(b, 0xFF26) & 1);
    assert(b->apu.ch[0].volume == 15);
    gb_bus_tick(b, 8192 * 8);
    assert(b->apu.ch[0].volume == 14);
    int16_t samples[GB_APU_BUFFER_FRAMES * 2];
    size_t count = gb_apu_take_samples(&b->apu, samples, GB_APU_BUFFER_FRAMES);
    assert(count > 0);
    bool any_nonzero = false;
    for (size_t i=0; i<count*2; ++i) if (samples[i]) { any_nonzero = true; break; }
    assert(any_nonzero);
    wr(b, 0xFF12, 0);
    assert((rd(b, 0xFF26) & 1) == 0);
}
static void test_length_noise(GB_Bus *b) {
    gb_bus_init(b, false);
    wr(b, 0xFF26, 0x80);
    wr(b, 0xFF20, 63); /* length = 1 */
    wr(b, 0xFF21, 0xF0);
    wr(b, 0xFF22, 0x00);
    wr(b, 0xFF23, 0xC0);
    assert(rd(b, 0xFF26) & 8);
    gb_bus_tick(b, 8192);
    assert((rd(b, 0xFF26) & 8) == 0);
}
static void test_bus_and_rate(GB_Bus *b) {
    gb_bus_init(b, false);
    gb_bus_tick(b, GB_APU_CLOCK / 20);
    assert(b->apu.pending_frames == ((size_t)(GB_APU_CLOCK / 20u) * GB_APU_RATE / GB_APU_CLOCK));
    int16_t out[GB_APU_BUFFER_FRAMES*2];
    size_t n = gb_apu_take_samples(&b->apu, out, 100);
    assert(n == 100);
    assert(b->apu.pending_frames == ((size_t)(GB_APU_CLOCK / 20u) * GB_APU_RATE / GB_APU_CLOCK) - 100);
    gb_apu_clear_samples(&b->apu);
    assert(b->apu.pending_frames == 0);
}
int main(void) {
    GB_Bus *b = malloc(sizeof(*b));
    assert(b);
    test_power(b); test_wave(b); test_square_and_envelope(b);
    test_length_noise(b); test_bus_and_rate(b);
    free(b);
    puts("APU tests passed.");
    return 0;
}
