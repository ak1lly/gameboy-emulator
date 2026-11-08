#ifndef GB_TIMER_H
#define GB_TIMER_H
#include <stdbool.h>
#include <stdint.h>

/* DMG timer: internal 16-bit divider, TIMA, TMA, TAC.
 * Tick in CPU T-cycles, not machine cycles. */
typedef struct {
    uint16_t div_counter;
    uint8_t tima, tma, tac;
    uint8_t reload_delay; /* cycles until overflow reload/IRQ; zero if idle */
} GB_Timer;

typedef void (*gb_timer_irq_fn)(void *ctx);
void gb_timer_init(GB_Timer *timer, bool start_after_boot);
void gb_timer_tick(GB_Timer *timer, unsigned tcycles, gb_timer_irq_fn irq, void *ctx);
uint8_t gb_timer_read(const GB_Timer *timer, uint16_t address);
void gb_timer_write(GB_Timer *timer, uint16_t address, uint8_t value);
#endif
