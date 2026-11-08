#ifndef GB_PPU_H
#define GB_PPU_H
#include <stdbool.h>
#include <stdint.h>
#define GB_LCD_WIDTH 160
#define GB_LCD_HEIGHT 144
/* Pixel values are DMG shades 0 (white) through 3 (black). */
typedef struct {
    uint8_t framebuffer[GB_LCD_HEIGHT][GB_LCD_WIDTH];
    unsigned dot;          /* Dot within scanline: 0..455. */
    uint8_t ly;
    uint8_t window_line;
    bool stat_line;
    bool frame_ready;
    uint64_t frames;
} GB_PPU;
struct GB_Bus;
void gb_ppu_init(GB_PPU *ppu, bool after_boot);
void gb_ppu_tick(struct GB_Bus *bus, unsigned cycles);
uint8_t gb_ppu_read(struct GB_Bus *bus, uint16_t address);
void gb_ppu_write(struct GB_Bus *bus, uint16_t address, uint8_t value);
#endif
