#include "bus.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void w(GB_Bus *b, uint16_t a, uint8_t v) { gb_bus_write(b, a, v); }
static uint8_t r(GB_Bus *b, uint16_t a) { return gb_bus_read(b, a); }
static void reset(GB_Bus *b) { gb_bus_init(b, true); w(b, 0xFF0F, 0); }

int main(void) {
    GB_Bus *b = malloc(sizeof(*b));
    assert(b);
    reset(b);
    assert(r(b, 0xFF44) == 0);
    assert((r(b, 0xFF41) & 3) == 2);
    gb_bus_tick(b, 80);
    assert((r(b, 0xFF41) & 3) == 3);
    gb_bus_tick(b, 172);
    assert((r(b, 0xFF41) & 3) == 0);
    gb_bus_tick(b, 204);
    assert(r(b, 0xFF44) == 1 && (r(b, 0xFF41) & 3) == 2);
    gb_bus_tick(b, 143 * 456);
    assert(r(b, 0xFF44) == 144 && (r(b, 0xFF41) & 3) == 1);
    assert((r(b, 0xFF0F) & 1) && b->ppu.frame_ready && b->ppu.frames == 1);
    b->ppu.frame_ready = false;
    gb_bus_tick(b, 10 * 456);
    assert(r(b, 0xFF44) == 0 && b->ppu.frames == 1);
    gb_bus_tick(b, 144 * 456);
    assert(b->ppu.frames == 2);

    reset(b);
    w(b, 0xFF41, 0x40); /* LYC equal IRQ enabled */
    assert(r(b, 0xFF0F) & 2);
    w(b, 0xFF0F, 0);
    gb_bus_tick(b, 456);
    w(b, 0xFF45, 1);
    assert(r(b, 0xFF41) & 4);
    assert(r(b, 0xFF0F) & 2);
    w(b, 0xFF0F, 0);
    gb_bus_tick(b, 100);
    assert(!(r(b, 0xFF0F) & 2)); /* STAT edge, not per-cycle */
    w(b, 0xFF40, 0);
    assert(r(b, 0xFF44) == 0 && (r(b, 0xFF41) & 3) == 0);
    gb_bus_tick(b, 456 * 2);
    assert(r(b, 0xFF44) == 0);
    w(b, 0xFF40, 0x91);
    assert((r(b, 0xFF41) & 3) == 2);
    w(b, 0xFF44, 77);
    assert(r(b, 0xFF44) == 0); /* LY readonly */

    /* Background: Tile #0 has raw color 1 at every pixel in top row. */
    reset(b);
    w(b, 0x8000, 0xFF); w(b, 0x8001, 0);
    w(b, 0xFF47, 0xE4); /* identity palette */
    gb_bus_tick(b, 252);
    assert(b->ppu.framebuffer[0][0] == 1);
    assert(b->ppu.framebuffer[0][159] == 1);

    /* Window overlays BG starting x=0 when WX=7. */
    reset(b);
    w(b, 0xFF47, 0xE4);
    w(b, 0x8000, 0xFF);
    w(b, 0x8010, 0x00); w(b, 0x8011, 0xFF); /* tile 1: color 2 */
    w(b, 0x9C00, 1);
    w(b, 0xFF4A, 0); w(b, 0xFF4B, 7);
    w(b, 0xFF40, 0xF1); /* LCD, window, upper window map, tiles, BG */
    gb_bus_tick(b, 252);
    assert(b->ppu.framebuffer[0][0] == 2);
    assert(b->ppu.window_line == 1);

    /* Sprite tile 1 has raw color 2; sprite sits at (0,0). */
    reset(b);
    w(b, 0xFF47, 0xE4); w(b, 0xFF48, 0xE4);
    w(b, 0x8010, 0); w(b, 0x8011, 0xFF);
    w(b, 0xFE00, 16); w(b, 0xFE01, 8);
    w(b, 0xFE02, 1); w(b, 0xFE03, 0);
    w(b, 0xFF40, 0x93);
    gb_bus_tick(b, 252);
    assert(b->ppu.framebuffer[0][0] == 2);
    /* Behind BG priority hides sprite when BG pixel is nonzero. */
    reset(b);
    w(b, 0xFF47, 0xE4); w(b, 0xFF48, 0xE4);
    w(b, 0x8000, 0xFF); w(b, 0x8010, 0); w(b, 0x8011, 0xFF);
    w(b, 0xFE00, 16); w(b, 0xFE01, 8);
    w(b, 0xFE02, 1); w(b, 0xFE03, 0x80);
    w(b, 0xFF40, 0x93);
    gb_bus_tick(b, 252);
    assert(b->ppu.framebuffer[0][0] == 1);

    free(b);
    puts("All PPU tests passed.");
    return 0;
}
