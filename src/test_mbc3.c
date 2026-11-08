#include "cartridge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *make_rom(unsigned banks, uint8_t type, uint8_t rom_code, uint8_t ram_code) {
    size_t size = (size_t)banks * 0x4000;
    uint8_t *rom = malloc(size);
    assert(rom);
    for (unsigned bank = 0; bank < banks; ++bank)
        memset(rom + (size_t)bank * 0x4000, (int)bank, 0x4000);
    rom[0x147] = type;
    rom[0x148] = rom_code;
    rom[0x149] = ram_code;
    return rom;
}

int main(void) {
    /* A 1 MiB ROM with 32 KiB RAM, exactly Pokémon Red's mapper/size setup. */
    const unsigned banks = 64;
    uint8_t *rom = make_rom(banks, 0x13, 5, 3);
    GB_Cartridge cart = {0};
    char err[128];
    assert(gb_cart_from_bytes(&cart, rom, (size_t)banks * 0x4000, err, sizeof(err)));
    assert(cart.mapper == GB_MAPPER_MBC3 && cart.has_battery && cart.ram_size == 32768);
    assert(gb_cart_read(&cart, 0x0000) == 0);
    assert(gb_cart_read(&cart, 0x4000) == 1);
    gb_cart_write(&cart, 0x2000, 5);
    assert(gb_cart_read(&cart, 0x4000) == 5);
    gb_cart_write(&cart, 0x2000, 0);
    assert(gb_cart_read(&cart, 0x4000) == 1);
    gb_cart_write(&cart, 0x2000, 0x45); /* seven-bit bank, modulo ROM size */
    assert(gb_cart_read(&cart, 0x4000) == 5);
    assert(gb_cart_read(&cart, 0xA000) == 0xFF);
    gb_cart_write(&cart, 0xA000, 0x77);
    gb_cart_write(&cart, 0x0000, 0x0A);
    gb_cart_write(&cart, 0x4000, 0);
    gb_cart_write(&cart, 0xA000, 0x10);
    gb_cart_write(&cart, 0x4000, 1);
    gb_cart_write(&cart, 0xA000, 0x21);
    gb_cart_write(&cart, 0x4000, 2);
    gb_cart_write(&cart, 0xA000, 0x32);
    gb_cart_write(&cart, 0x4000, 3);
    gb_cart_write(&cart, 0xA000, 0x43);
    for (uint8_t i = 0; i < 4; ++i) {
        gb_cart_write(&cart, 0x4000, i);
        assert(gb_cart_read(&cart, 0xA000) == (uint8_t)(0x10 + 0x11 * i));
    }
    gb_cart_write(&cart, 0x4000, 0x08); /* RTC selection: no RTC here */
    assert(gb_cart_read(&cart, 0xA000) == 0xFF);
    gb_cart_write(&cart, 0xA000, 0x99);
    gb_cart_write(&cart, 0x4000, 0);
    assert(gb_cart_read(&cart, 0xA000) == 0x10);
    gb_cart_write(&cart, 0x0000, 0);
    assert(gb_cart_read(&cart, 0xA000) == 0xFF);
    gb_cart_write(&cart, 0xA000, 0x55);
    gb_cart_write(&cart, 0x0000, 0x1A); /* low nibble 0xA enables RAM */
    assert(gb_cart_read(&cart, 0xA000) == 0x10);

    const char *rom_name = "test_mbc3_tmp.gb";
    const char *save_name = "test_mbc3_tmp.sav";
    remove(save_name);
    assert(gb_cart_auto_load_save(&cart, rom_name)); /* missing save is OK */
    assert(gb_cart_auto_write_save(&cart, rom_name));
    memset(cart.ram, 0, cart.ram_size);
    assert(gb_cart_auto_load_save(&cart, rom_name));
    assert(gb_cart_read(&cart, 0xA000) == 0x10);
    gb_cart_write(&cart, 0x4000, 3);
    assert(gb_cart_read(&cart, 0xA000) == 0x43);
    remove(save_name);
    gb_cart_free(&cart);
    free(rom);

    /* MBC3 without RAM must still be accepted. */
    rom = make_rom(2, 0x11, 0, 0);
    assert(gb_cart_from_bytes(&cart, rom, 0x8000, err, sizeof(err)));
    assert(cart.mapper == GB_MAPPER_MBC3 && cart.ram == NULL);
    gb_cart_free(&cart);
    free(rom);
    puts("All MBC3 cartridge and battery-save tests passed.");
    return 0;
}
