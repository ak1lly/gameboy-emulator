#include "bus.h"
#include "cpu.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Export the latest completed (or partially rendered) DMG framebuffer as PPM.
 * PPM P6 can be opened by ImageMagick, GIMP, and many image viewers. */
static bool write_ppm(const char *path, const GB_PPU *ppu) {
    static const unsigned char colors[4][3] = {
        {224, 248, 208}, {136, 192, 112}, {52, 104, 86}, {8, 24, 32}
    };
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fprintf(f, "P6\n160 144\n255\n") > 0;
    for (unsigned y = 0; y < GB_LCD_HEIGHT && ok; ++y) {
        for (unsigned x = 0; x < GB_LCD_WIDTH; ++x) {
            unsigned shade = ppu->framebuffer[y][x] & 3;
            if (fwrite(colors[shade], 1, 3, f) != 3) { ok = false; break; }
        }
    }
    if (fclose(f) != 0) ok = false;
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: %s path/to/game.gb [max-instructions] [frame.ppm]\n", argv[0]);
        return 1;
    }
    uint64_t limit = 100000;
    if (argc >= 3) {
        char *end;
        limit = strtoull(argv[2], &end, 10);
        if (*end) { fprintf(stderr, "Invalid instruction limit\n"); return 1; }
    }
    GB_Bus *bus = malloc(sizeof(*bus));
    if (!bus) return 1;
    gb_bus_init(bus, true);
    char error[128];
    if (!gb_cart_load(&bus->cart, argv[1], error, sizeof(error))) {
        fprintf(stderr, "Load error: %s\n", error);
        free(bus);
        return 1;
    }
    printf("Loaded: %s (type %02X, %u ROM banks, %zu RAM bytes)\n",
           bus->cart.title, bus->cart.type, bus->cart.rom_banks, bus->cart.ram_size);
    if (!gb_cart_auto_load_save(&bus->cart, argv[1])) {
        fprintf(stderr, "Warning: could not load existing battery save (.sav).\n");
    }
    GB_CPU cpu;
    gb_cpu_init(&cpu, bus, gb_bus_read, gb_bus_write, true);
    uint64_t executed = 0;
    for (; executed < limit; ++executed) {
        unsigned clocks = gb_cpu_step(&cpu);
        if (!clocks || cpu.stopped) break;
        gb_bus_tick(bus, clocks); /* Advance timers even during CPU HALT. */
        /* gb_bus_tick advances timer, PPU, and APU. */
    }
    printf("Executed: %" PRIu64 ", PC=%04X, A=%02X, F=%02X, SP=%04X, T-cycles=%" PRIu64 "%s\n",
           executed, cpu.pc, cpu.a, cpu.f, cpu.sp, cpu.cycles,
           cpu.invalid_opcode ? " (invalid opcode)" : "");
    printf("Rendered frames: %" PRIu64 "\n", bus->ppu.frames);
    if (argc == 4) {
        if (!write_ppm(argv[3], &bus->ppu)) {
            fprintf(stderr, "Could not write screenshot: %s\n", argv[3]);
            gb_cart_auto_write_save(&bus->cart, argv[1]);
            gb_cart_free(&bus->cart);
            free(bus);
            return 1;
        }
        printf("Saved framebuffer to %s\n", argv[3]);
    }
    if (!gb_cart_auto_write_save(&bus->cart, argv[1]))
        fprintf(stderr, "Warning: could not write battery save (.sav).\n");
    gb_cart_free(&bus->cart);
    free(bus);
    return cpu.invalid_opcode ? 1 : 0;
}
