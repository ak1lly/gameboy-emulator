#ifndef GB_BUS_H
#define GB_BUS_H
#include "cartridge.h"
#include "timer.h"
#include "ppu.h"
#include "apu.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct GB_Bus {
    GB_Cartridge cart;
    GB_PPU ppu;
    GB_APU apu;
    GB_Timer timer;
    uint8_t vram[0x2000], wram[0x2000], oam[0xA0], io[0x80], hram[0x7F];
    uint8_t ie;
    /* Buttons active high in this API: right,left,up,down,A,B,select,start */
    uint8_t buttons;
    uint8_t boot_rom[0x100];
    bool boot_enabled;
} GB_Bus;

void gb_bus_init(GB_Bus *bus, bool start_after_boot);
/* Interrupt bits: VBlank=0, LCD STAT=1, Timer=2, Serial=3, Joypad=4. */
enum { GB_INT_VBLANK = 0, GB_INT_STAT = 1, GB_INT_TIMER = 2,
       GB_INT_SERIAL = 3, GB_INT_JOYPAD = 4 };
void gb_bus_request_interrupt(GB_Bus *bus, unsigned bit);
void gb_bus_tick(GB_Bus *bus, unsigned tcycles);
void gb_bus_set_buttons(GB_Bus *bus, uint8_t pressed_mask);
uint8_t gb_bus_read(void *ctx, uint16_t address);
void gb_bus_write(void *ctx, uint16_t address, uint8_t value);
/* Optional boot ROM data mapping at 0000-00FF until FF50 written nonzero. */
void gb_bus_set_boot_rom(GB_Bus *bus, const uint8_t boot[0x100]);
#endif
