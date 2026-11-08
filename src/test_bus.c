#include "bus.h"
#include "cpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mkrom(uint8_t *r, unsigned banks, uint8_t kind, uint8_t romsize, uint8_t ramsize) {
    memset(r, 0, (size_t)banks * 0x4000);
    for (unsigned bank = 0; bank < banks; ++bank)
        memset(r + (size_t)bank * 0x4000, bank, 0x4000);
    r[0x147] = kind; r[0x148] = romsize; r[0x149] = ramsize;
}
int main(void) {
    GB_Bus *b = malloc(sizeof(*b));
    assert(b);
    gb_bus_init(b, true);
    char err[128];
    uint8_t *rom = malloc(4 * 0x4000);
    assert(rom);
    mkrom(rom, 4, 0x03, 1, 3);
    assert(gb_cart_from_bytes(&b->cart, rom, 4 * 0x4000, err, sizeof(err)));
    assert(gb_bus_read(b, 0x100) == 0 && gb_bus_read(b, 0x4000) == 1);
    gb_bus_write(b, 0x2000, 2);
    assert(gb_bus_read(b, 0x4000) == 2);
    gb_bus_write(b, 0x2000, 0);
    assert(gb_bus_read(b, 0x4000) == 1); /* bank 0 remaps to 1 */
    assert(gb_bus_read(b, 0xA000) == 0xFF);
    gb_bus_write(b, 0x0000, 0x0A);
    gb_bus_write(b, 0xA000, 0x88);
    assert(gb_bus_read(b, 0xA000) == 0x88);
    gb_bus_write(b, 0x6000, 1);
    gb_bus_write(b, 0x4000, 2);
    gb_bus_write(b, 0xA000, 0x59);
    assert(gb_bus_read(b, 0xA000) == 0x59);
    gb_bus_write(b, 0x4000, 0);
    assert(gb_bus_read(b, 0xA000) == 0x88);
    assert(gb_cart_save_ram(&b->cart, "test_ram_tmp.sav"));
    memset(b->cart.ram, 0, b->cart.ram_size);
    assert(gb_cart_load_ram(&b->cart, "test_ram_tmp.sav"));
    assert(gb_bus_read(b, 0xA000) == 0x88);
    remove("test_ram_tmp.sav");

    gb_bus_write(b, 0xC123, 0x52);
    assert(gb_bus_read(b, 0xE123) == 0x52);
    gb_bus_write(b, 0xFDFF, 0xA7);
    assert(gb_bus_read(b, 0xDDFF) == 0xA7);
    gb_bus_write(b, 0xFEA0, 0x11);
    assert(gb_bus_read(b, 0xFEA0) == 0xFF);
    gb_bus_write(b, 0xFFFF, 0x1F);
    assert(gb_bus_read(b, 0xFFFF) == 0x1F);
    gb_bus_write(b, 0xFF0F, 1);
    assert(gb_bus_read(b, 0xFF0F) == 0xE1);
    gb_bus_write(b, 0xFF00, 0x10); /* select action keys */
    gb_bus_set_buttons(b, 0x10); /* A */
    assert((gb_bus_read(b, 0xFF00) & 0x0F) == 0x0E);
    for (unsigned i = 0; i < 160; ++i) gb_bus_write(b, 0xC000 + i, (uint8_t)i);
    gb_bus_write(b, 0xFF46, 0xC0);
    for (unsigned i = 0; i < 160; ++i) assert(gb_bus_read(b, 0xFE00 + i) == (uint8_t)i);

    uint8_t boot[256]; memset(boot, 0xEA, sizeof(boot));
    gb_bus_set_boot_rom(b, boot);
    assert(gb_bus_read(b, 0) == 0xEA);
    gb_bus_write(b, 0xFF50, 1);
    assert(gb_bus_read(b, 0) == 0);

    gb_cart_free(&b->cart);
    mkrom(rom, 4, 0, 1, 0);
    assert(gb_cart_from_bytes(&b->cart, rom, 4 * 0x4000, err, sizeof(err)));
    gb_bus_write(b, 0x2000, 3); /* ignored for ROM-only */
    assert(gb_bus_read(b, 0x4000) == 1);
    assert(gb_bus_read(b, 0xA000) == 0xFF);
    gb_cart_free(&b->cart);

    mkrom(rom, 4, 0x19, 1, 0);
    assert(!gb_cart_from_bytes(&b->cart, rom, 4 * 0x4000, err, sizeof(err)));
    mkrom(rom, 4, 0, 0, 0);
    assert(!gb_cart_from_bytes(&b->cart, rom, 4 * 0x4000, err, sizeof(err)));

    /* Integration: execute a program from ROM through CPU/bus callbacks. */
    mkrom(rom, 4, 0, 1, 0);
    const uint8_t program[] = {0x06, 5, 0x0E, 3, 0x78, 0x81, 0xEA, 0x00, 0xC0, 0x76};
    memcpy(rom + 0x100, program, sizeof(program));
    assert(gb_cart_from_bytes(&b->cart, rom, 4 * 0x4000, err, sizeof(err)));
    gb_bus_write(b, 0xFFFF, 0);
    gb_bus_write(b, 0xFF0F, 0);
    GB_CPU cpu;
    gb_cpu_init(&cpu, b, gb_bus_read, gb_bus_write, true);
    for (int i = 0; i < 6; ++i) assert(gb_cpu_step(&cpu));
    assert(cpu.a == 8 && cpu.halted && gb_bus_read(b, 0xC000) == 8);
    gb_cart_free(&b->cart);
    free(rom);
    free(b);
    puts("All bus/cartridge integration tests passed.");
    return 0;
}
