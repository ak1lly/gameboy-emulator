#include "bus.h"
#include "cpu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void reset(GB_Bus *b) { gb_bus_init(b, false); }
static void tick(GB_Bus *b, unsigned n) { gb_bus_tick(b, n); }
static uint8_t rd(GB_Bus *b, unsigned a) { return gb_bus_read(b, (uint16_t)a); }
static void wr(GB_Bus *b, unsigned a, unsigned v) { gb_bus_write(b, (uint16_t)a, (uint8_t)v); }

static void test_divider(GB_Bus *b) {
    reset(b);
    assert(rd(b, 0xFF04) == 0);
    tick(b, 255); assert(rd(b, 0xFF04) == 0);
    tick(b, 1); assert(rd(b, 0xFF04) == 1);
    tick(b, 256); assert(rd(b, 0xFF04) == 2);
    wr(b, 0xFF04, 0x99); assert(rd(b, 0xFF04) == 0);
    tick(b, 256); assert(rd(b, 0xFF04) == 1);
}

static void test_frequencies(GB_Bus *b) {
    const unsigned periods[4] = {1024, 16, 64, 256};
    for (unsigned sel = 0; sel < 4; ++sel) {
        reset(b);
        wr(b, 0xFF07, 4 | sel);
        assert(rd(b, 0xFF07) == (0xFC | sel));
        tick(b, periods[sel] - 1); assert(rd(b, 0xFF05) == 0);
        tick(b, 1); assert(rd(b, 0xFF05) == 1);
        tick(b, periods[sel] * 2); assert(rd(b, 0xFF05) == 3);
        wr(b, 0xFF07, sel); tick(b, periods[sel] * 2);
        assert(rd(b, 0xFF05) == 3);
    }
}

static void test_overflow(GB_Bus *b) {
    reset(b);
    wr(b, 0xFF0F, 0);
    wr(b, 0xFF06, 0xA5);
    wr(b, 0xFF05, 0xFF);
    wr(b, 0xFF07, 0x05);
    tick(b, 16);
    assert(rd(b, 0xFF05) == 0x00);
    assert((rd(b, 0xFF0F) & 4) == 0);
    tick(b, 3); assert(rd(b, 0xFF05) == 0);
    tick(b, 1); assert(rd(b, 0xFF05) == 0xA5);
    assert((rd(b, 0xFF0F) & 4) != 0);
    wr(b, 0xFF0F, 0);
    tick(b, 12); assert(rd(b, 0xFF05) == 0xA6);

    /* TIMA write during pending reload cancels the reload and IRQ. */
    reset(b); wr(b, 0xFF0F, 0); wr(b, 0xFF07, 5);
    wr(b, 0xFF05, 0xFF); tick(b, 16);
    wr(b, 0xFF05, 0x23); tick(b, 4);
    assert(rd(b, 0xFF05) == 0x23);
    assert((rd(b, 0xFF0F) & 4) == 0);

    /* Writing TMA during overflow changes the value that gets reloaded. */
    reset(b); wr(b, 0xFF0F, 0); wr(b, 0xFF07, 5);
    wr(b, 0xFF05, 0xFF); tick(b, 16);
    wr(b, 0xFF06, 0x6C); tick(b, 4);
    assert(rd(b, 0xFF05) == 0x6C);
}

static void test_edge_glitches(GB_Bus *b) {
    reset(b); wr(b, 0xFF07, 5);
    tick(b, 8); /* bit 3 is high */
    wr(b, 0xFF04, 0); /* divider reset creates falling edge */
    assert(rd(b, 0xFF05) == 1);
    tick(b, 8);
    wr(b, 0xFF07, 0); /* disabling timer while input high */
    assert(rd(b, 0xFF05) == 2);
    reset(b); wr(b, 0xFF07, 5); tick(b, 8);
    wr(b, 0xFF07, 4); /* select bit 9 (low) instead of bit 3 (high) */
    assert(rd(b, 0xFF05) == 1);
}

static void test_interrupt_helpers(GB_Bus *b) {
    reset(b);
    wr(b, 0xFF0F, 0);
    gb_bus_request_interrupt(b, GB_INT_VBLANK);
    gb_bus_request_interrupt(b, GB_INT_STAT);
    gb_bus_request_interrupt(b, GB_INT_SERIAL);
    gb_bus_request_interrupt(b, 99);
    assert((rd(b, 0xFF0F) & 0x1F) == 0x0B);
    wr(b, 0xFF0F, 0);
    wr(b, 0xFF00, 0x10); /* select action buttons */
    gb_bus_set_buttons(b, 0x10); /* A pressed -> falling P1 edge */
    assert((rd(b, 0xFF0F) & 0x10) != 0);
    wr(b, 0xFF0F, 0);
    gb_bus_set_buttons(b, 0x10); /* held, no new edge */
    assert((rd(b, 0xFF0F) & 0x10) == 0);
    gb_bus_set_buttons(b, 0); /* release, no IRQ */
    assert((rd(b, 0xFF0F) & 0x10) == 0);
    gb_bus_set_buttons(b, 0x10);
    assert((rd(b, 0xFF0F) & 0x10) != 0);
}

static void test_cpu_interrupt(GB_Bus *b) {
    /* Program in HRAM so that there is no cartridge dependency. */
    reset(b);
    wr(b, 0xFF80, 0xFB); /* EI */
    wr(b, 0xFF81, 0x00); /* NOP (EI delayed enable) */
    wr(b, 0xFF82, 0x76); /* HALT */
    wr(b, 0xFF83, 0x00); /* NOP */
    GB_CPU cpu;
    gb_cpu_init(&cpu, b, gb_bus_read, gb_bus_write, false);
    cpu.pc = 0xFF80; cpu.sp = 0xFFFE;
    wr(b, 0xFFFF, 0x04); /* enable timer IRQ */
    wr(b, 0xFF0F, 0);
    wr(b, 0xFF07, 0x05); /* 16-cycle period */
    wr(b, 0xFF05, 0xFF); wr(b, 0xFF06, 0x91);
    unsigned cycles;
    cycles = gb_cpu_step(&cpu); tick(b, cycles); assert(!cpu.ime);
    cycles = gb_cpu_step(&cpu); tick(b, cycles); assert(cpu.ime);
    cycles = gb_cpu_step(&cpu); tick(b, cycles); assert(cpu.halted);
    cycles = gb_cpu_step(&cpu); tick(b, cycles); assert(cpu.halted);
    cycles = gb_cpu_step(&cpu); tick(b, cycles); /* overflow reload raises IF */
    assert((rd(b, 0xFF0F) & 4) != 0);
    cycles = gb_cpu_step(&cpu); tick(b, cycles);
    assert(!cpu.halted && !cpu.ime && cpu.pc == 0x0050);
    assert((rd(b, 0xFF0F) & 4) == 0);
    assert(cpu.sp == 0xFFFC);
    assert(rd(b, 0xFFFC) == 0x83 && rd(b, 0xFFFD) == 0xFF);
}

int main(void) {
    GB_Bus b;
    test_divider(&b);
    test_frequencies(&b);
    test_overflow(&b);
    test_edge_glitches(&b);
    test_interrupt_helpers(&b);
    test_cpu_interrupt(&b);
    puts("All timer/interrupt tests passed.");
    return 0;
}
