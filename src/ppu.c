#include "bus.h"
#include <string.h>

/* LCD STAT is a level-triggered source internally: IF receives a request on
 * a low-to-high transition of the combined enabled STAT conditions. */
static bool lcd_on(const GB_Bus *b) { return (b->io[0x40] & 0x80) != 0; }
static unsigned ppu_mode(const GB_Bus *b) {
    if (!lcd_on(b)) return 0;
    if (b->ppu.ly >= 144) return 1;
    if (b->ppu.dot < 80) return 2;
    if (b->ppu.dot < 252) return 3; /* fixed mode 3 duration approximation */
    return 0;
}
static void update_stat(GB_Bus *b) {
    GB_PPU *p = &b->ppu;
    uint8_t stat = b->io[0x41];
    bool equal = p->ly == b->io[0x45];
    stat = (uint8_t)((stat & 0xF8) | ppu_mode(b) | (equal ? 4 : 0) | 0x80);
    b->io[0x41] = stat;
    bool line = lcd_on(b) && (((stat & 0x40) && equal) ||
                  ((stat & 0x20) && ppu_mode(b) == 2) ||
                  ((stat & 0x10) && ppu_mode(b) == 1) ||
                  ((stat & 0x08) && ppu_mode(b) == 0));
    if (line && !p->stat_line) gb_bus_request_interrupt(b, GB_INT_STAT);
    p->stat_line = line;
}
void gb_ppu_init(GB_PPU *p, bool after_boot) {
    memset(p, 0, sizeof(*p));
    (void)after_boot;
}
static uint8_t tile_pixel(const GB_Bus *b, unsigned map_base, unsigned tile_x,
                          unsigned tile_y, unsigned pixel_x, unsigned pixel_y,
                          bool unsigned_tiles) {
    unsigned entry = map_base + ((tile_y & 31) * 32) + (tile_x & 31);
    uint8_t id = b->vram[entry];
    unsigned tile_addr;
    if (unsigned_tiles) tile_addr = (unsigned)id * 16;
    else tile_addr = (unsigned)(0x1000 + (int8_t)id * 16);
    unsigned row = tile_addr + 2 * (pixel_y & 7);
    uint8_t lo = b->vram[row], hi = b->vram[row + 1];
    unsigned shift = 7 - (pixel_x & 7);
    return (uint8_t)((((hi >> shift) & 1) << 1) | ((lo >> shift) & 1));
}
static uint8_t palette(uint8_t reg, uint8_t index) {
    return (uint8_t)((reg >> (index * 2)) & 3);
}
static void render_scanline(GB_Bus *b) {
    GB_PPU *p = &b->ppu;
    unsigned y = p->ly;
    uint8_t lcdc = b->io[0x40];
    uint8_t bg_raw[GB_LCD_WIDTH] = {0};
    uint8_t *line = p->framebuffer[y];
    bool draw_window = (lcdc & 0x20) && (lcdc & 1) && y >= b->io[0x4A] && b->io[0x4B] <= 166;
    bool used_window = false;
    for (unsigned x = 0; x < GB_LCD_WIDTH; x++) {
        uint8_t raw = 0;
        if (lcdc & 1) {
            bool win = draw_window && (int)x >= (int)b->io[0x4B] - 7;
            unsigned px, py, map;
            if (win) {
                used_window = true;
                px = (unsigned)((int)x - ((int)b->io[0x4B] - 7));
                py = p->window_line;
                map = (lcdc & 0x40) ? 0x1C00 : 0x1800;
            } else {
                px = (x + b->io[0x43]) & 255;
                py = (y + b->io[0x42]) & 255;
                map = (lcdc & 0x08) ? 0x1C00 : 0x1800;
            }
            raw = tile_pixel(b, map, px / 8, py / 8, px, py, (lcdc & 0x10) != 0);
        }
        bg_raw[x] = raw;
        line[x] = palette(b->io[0x47], raw);
    }
    if (used_window) p->window_line++;
    if (!(lcdc & 2)) return;
    unsigned h = (lcdc & 4) ? 16 : 8;
    /* Pick first 10 OAM entries intersecting this line. DMG sprite priority
     * among them: smaller X coordinate wins; ties go to earlier OAM entry. */
    unsigned candidates[10], count = 0;
    for (unsigned i = 0; i < 40 && count < 10; i++) {
        int top = (int)b->oam[i * 4] - 16;
        if ((int)y >= top && (int)y < top + (int)h) candidates[count++] = i;
    }
    for (unsigned x = 0; x < GB_LCD_WIDTH; x++) {
        int winner = -1, winner_x = 256;
        uint8_t winner_color = 0, winner_flags = 0;
        for (unsigned j = 0; j < count; j++) {
            unsigned i = candidates[j];
            int sx = (int)b->oam[4 * i + 1] - 8;
            if ((int)x < sx || (int)x >= sx + 8) continue;
            uint8_t flags = b->oam[4 * i + 3];
            unsigned row = (unsigned)((int)y - ((int)b->oam[4 * i] - 16));
            unsigned col = (unsigned)((int)x - sx);
            if (flags & 0x40) row = h - 1 - row;
            if (flags & 0x20) col = 7 - col;
            unsigned tile = b->oam[4 * i + 2];
            if (h == 16) tile &= ~1u;
            unsigned addr = tile * 16 + row * 2;
            uint8_t lo = b->vram[addr], hi = b->vram[addr + 1];
            unsigned shift = 7 - col;
            uint8_t color = (uint8_t)((((hi >> shift) & 1) << 1) | ((lo >> shift) & 1));
            if (!color) continue;
            if (sx < winner_x) { winner = (int)i; winner_x = sx; winner_color = color; winner_flags = flags; }
        }
        if (winner >= 0 && (!(winner_flags & 0x80) || bg_raw[x] == 0))
            line[x] = palette(b->io[(winner_flags & 0x10) ? 0x49 : 0x48], winner_color);
    }
}
void gb_ppu_tick(GB_Bus *b, unsigned cycles) {
    GB_PPU *p = &b->ppu;
    if (!lcd_on(b)) { update_stat(b); return; }
    /* Dot-stepping ensures correct mode/LY boundaries even with large ticks. */
    for (unsigned c = 0; c < cycles; c++) {
        ++p->dot;
        if (p->ly < 144 && p->dot == 252) render_scanline(b);
        if (p->dot >= 456) {
            p->dot = 0;
            ++p->ly;
            if (p->ly == 144) {
                gb_bus_request_interrupt(b, GB_INT_VBLANK);
                p->frame_ready = true;
                ++p->frames;
            }
            if (p->ly == 154) {
                p->ly = 0;
                p->window_line = 0;
            }
        }
        update_stat(b);
    }
}
uint8_t gb_ppu_read(GB_Bus *b, uint16_t a) {
    switch (a) {
        case 0xFF41: update_stat(b); return b->io[0x41];
        case 0xFF44: return b->ppu.ly;
        default: return b->io[a - 0xFF00];
    }
}
void gb_ppu_write(GB_Bus *b, uint16_t a, uint8_t v) {
    GB_PPU *p = &b->ppu;
    unsigned r = a - 0xFF00;
    if (a == 0xFF40) {
        bool old_on = lcd_on(b);
        b->io[r] = v;
        if (old_on && !(v & 0x80)) {
            p->ly = 0; p->dot = 0; p->window_line = 0;
            p->stat_line = false;
            memset(p->framebuffer, 0, sizeof(p->framebuffer));
        } else if (!old_on && (v & 0x80)) {
            p->ly = 0; p->dot = 0; p->window_line = 0;
            p->stat_line = false;
        }
    } else if (a == 0xFF41) {
        b->io[r] = (uint8_t)((b->io[r] & 0x87) | (v & 0x78));
    } else if (a != 0xFF44) { /* LY is read-only. */
        b->io[r] = v;
    }
    update_stat(b);
}
