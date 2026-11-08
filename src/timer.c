#include "timer.h"
#include <string.h>

/* TIMA increments on the falling edge of this signal:
 * TAC bit 2 enables it; TAC bits 1:0 select divider bit 9,3,5,7. */
static bool timer_signal(const GB_Timer *t) {
    static const unsigned bits[4] = {9, 3, 5, 7};
    return (t->tac & 4u) && ((t->div_counter >> bits[t->tac & 3u]) & 1u);
}

static void timer_falling_edge(GB_Timer *t) {
    if (t->reload_delay) return;
    if (t->tima == 0xFF) {
        t->tima = 0;
        t->reload_delay = 4; /* delayed reload and IRQ after four T-cycles */
    } else {
        ++t->tima;
    }
}

void gb_timer_init(GB_Timer *t, bool start_after_boot) {
    memset(t, 0, sizeof(*t));
    /* Approximate DMG divider state with boot ROM skipped. */
    t->div_counter = start_after_boot ? 0xABCCu : 0;
}

void gb_timer_tick(GB_Timer *t, unsigned tcycles, gb_timer_irq_fn irq, void *ctx) {
    for (unsigned i = 0; i < tcycles; ++i) {
        /* Resolve an earlier overflow before producing the current edge. */
        if (t->reload_delay && --t->reload_delay == 0) {
            t->tima = t->tma;
            if (irq) irq(ctx);
        }
        bool old_signal = timer_signal(t);
        ++t->div_counter;
        if (old_signal && !timer_signal(t)) timer_falling_edge(t);
    }
}

uint8_t gb_timer_read(const GB_Timer *t, uint16_t address) {
    switch (address) {
        case 0xFF04: return (uint8_t)(t->div_counter >> 8);
        case 0xFF05: return t->tima;
        case 0xFF06: return t->tma;
        case 0xFF07: return (uint8_t)(t->tac | 0xF8);
        default: return 0xFF;
    }
}

void gb_timer_write(GB_Timer *t, uint16_t address, uint8_t value) {
    bool old_signal = timer_signal(t);
    switch (address) {
        case 0xFF04:
            t->div_counter = 0;
            break;
        case 0xFF05:
            t->tima = value;
            t->reload_delay = 0; /* TIMA write cancels pending reload */
            break;
        case 0xFF06:
            t->tma = value;
            break;
        case 0xFF07:
            t->tac = (uint8_t)(value & 7u);
            break;
        default: return;
    }
    if (old_signal && !timer_signal(t)) timer_falling_edge(t);
}
