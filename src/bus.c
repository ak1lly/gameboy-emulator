#include "bus.h"
#include <string.h>

void gb_bus_init(GB_Bus *b, bool start_after_boot) {
    memset(b, 0, sizeof(*b));
    gb_timer_init(&b->timer, start_after_boot);
    gb_ppu_init(&b->ppu, start_after_boot);
    gb_apu_init(&b->apu, start_after_boot);
    b->io[0x00] = 0xCF; /* P1 */
    b->io[0x0F] = 0xE1; /* IF */
    if (start_after_boot) {
        /* Selected DMG post-boot I/O values; not a replacement for hardware emulation. */
        b->io[0x05] = 0x00; b->io[0x06] = 0x00; b->io[0x07] = 0xF8;
        b->io[0x10] = 0x80; b->io[0x11] = 0xBF; b->io[0x12] = 0xF3;
        b->io[0x14] = 0xBF; b->io[0x16] = 0x3F; b->io[0x17] = 0x00;
        b->io[0x19] = 0xBF; b->io[0x1A] = 0x7F; b->io[0x1B] = 0xFF;
        b->io[0x1C] = 0x9F; b->io[0x1E] = 0xBF; b->io[0x20] = 0xFF;
        b->io[0x21] = 0x00; b->io[0x22] = 0x00; b->io[0x23] = 0xBF;
        b->io[0x24] = 0x77; b->io[0x25] = 0xF3; b->io[0x26] = 0xF1;
        b->io[0x40] = 0x91; b->io[0x41] = 0x85; b->io[0x42] = 0;
        b->io[0x43] = 0; b->io[0x45] = 0; b->io[0x47] = 0xFC;
        b->io[0x48] = 0xFF; b->io[0x49] = 0xFF;
        b->io[0x4A] = 0; b->io[0x4B] = 0;
        b->io[0x50] = 1;
    }
}
void gb_bus_set_boot_rom(GB_Bus *b, const uint8_t data[0x100]) {
    memcpy(b->boot_rom, data, 0x100);
    b->boot_enabled = true;
    b->io[0x50] = 0;
}
void gb_bus_request_interrupt(GB_Bus *b, unsigned bit) {
    if (bit < 5) b->io[0x0F] |= (uint8_t)(1u << bit);
}
static void timer_irq(void *ctx) {
    gb_bus_request_interrupt((GB_Bus *)ctx, GB_INT_TIMER);
}
void gb_bus_tick(GB_Bus *b, unsigned tcycles) {
    gb_timer_tick(&b->timer, tcycles, timer_irq, b);
    gb_ppu_tick(b, tcycles);
    gb_apu_tick(&b->apu, tcycles);
}
static uint8_t joypad(const GB_Bus *b);
void gb_bus_set_buttons(GB_Bus *b, uint8_t mask) {
    uint8_t old = joypad(b);
    b->buttons = mask;
    if ((old & (uint8_t)~joypad(b) & 0x0F) != 0)
        gb_bus_request_interrupt(b, GB_INT_JOYPAD);
}
static uint8_t joypad(const GB_Bus *b) {
    uint8_t result = (uint8_t)(0xC0 | (b->io[0] & 0x30) | 0x0F);
    if (!(result & 0x10)) result &= (uint8_t)~(b->buttons & 0x0F);
    if (!(result & 0x20)) result &= (uint8_t)~((b->buttons >> 4) & 0x0F);
    return result;
}
uint8_t gb_bus_read(void *ctx, uint16_t a) {
    GB_Bus *b = ctx;
    if (a < 0x8000) {
        if (b->boot_enabled && a < 0x100) return b->boot_rom[a];
        return gb_cart_read(&b->cart, a);
    }
    if (a < 0xA000) return b->vram[a - 0x8000];
    if (a < 0xC000) return gb_cart_read(&b->cart, a);
    if (a < 0xE000) return b->wram[a - 0xC000];
    if (a < 0xFE00) return b->wram[a - 0xE000]; /* echo of C000-DDFF */
    if (a < 0xFEA0) return b->oam[a - 0xFE00];
    if (a < 0xFF00) return 0xFF; /* unusable */
    if (a < 0xFF80) {
        if (a == 0xFF00) return joypad(b);
        if (a >= 0xFF10 && a <= 0xFF3F) return gb_apu_read(&b->apu, a);
        if (a >= 0xFF04 && a <= 0xFF07) return gb_timer_read(&b->timer, a);
        if (a == 0xFF0F) return b->io[0x0F] | 0xE0;
        if (a >= 0xFF40 && a <= 0xFF4B && a != 0xFF46) return gb_ppu_read(b, a);
        return b->io[a - 0xFF00];
    }
    if (a < 0xFFFF) return b->hram[a - 0xFF80];
    return b->ie;
}
void gb_bus_write(void *ctx, uint16_t a, uint8_t v) {
    GB_Bus *b = ctx;
    if (a < 0x8000) { gb_cart_write(&b->cart, a, v); return; }
    if (a < 0xA000) { b->vram[a - 0x8000] = v; return; }
    if (a < 0xC000) { gb_cart_write(&b->cart, a, v); return; }
    if (a < 0xE000) { b->wram[a - 0xC000] = v; return; }
    if (a < 0xFE00) { b->wram[a - 0xE000] = v; return; }
    if (a < 0xFEA0) { b->oam[a - 0xFE00] = v; return; }
    if (a < 0xFF00) return;
    if (a < 0xFF80) {
        unsigned reg = a - 0xFF00;
        if (a == 0xFF00) {
            uint8_t old = joypad(b);
            b->io[0] = (uint8_t)((b->io[0] & ~0x30) | (v & 0x30));
            if ((old & (uint8_t)~joypad(b) & 0x0F) != 0)
                gb_bus_request_interrupt(b, GB_INT_JOYPAD);
            return;
        }
        if (a == 0xFF0F) { b->io[reg] = v | 0xE0; return; }
        if (a >= 0xFF10 && a <= 0xFF3F) { gb_apu_write(&b->apu, a, v); return; }
        if (a >= 0xFF04 && a <= 0xFF07) { gb_timer_write(&b->timer, a, v); return; }
        if (a >= 0xFF40 && a <= 0xFF4B && a != 0xFF46) { gb_ppu_write(b, a, v); return; }
        if (a == 0xFF46) {
            b->io[reg] = v;
            /* Instant DMA for now; actual bus lockout/timing handled later. */
            uint16_t base = (uint16_t)v << 8;
            for (unsigned i = 0; i < 0xA0; ++i)
                b->oam[i] = gb_bus_read(b, (uint16_t)(base + i));
            return;
        }
        if (a == 0xFF50) {
            b->io[reg] = v;
            if (v) b->boot_enabled = false; /* boot ROM cannot be reenabled */
            return;
        }
        b->io[reg] = v;
        return;
    }
    if (a < 0xFFFF) { b->hram[a - 0xFF80] = v; return; }
    b->ie = v;
}
