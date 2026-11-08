#ifndef GB_CARTRIDGE_H
#define GB_CARTRIDGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { GB_MAPPER_ROM, GB_MAPPER_MBC1, GB_MAPPER_MBC3 } GB_Mapper;
typedef struct {
    uint8_t *rom, *ram;
    size_t rom_size, ram_size;
    unsigned rom_banks, ram_banks;
    uint8_t type;
    GB_Mapper mapper;
    bool has_battery, ram_enabled;
    uint8_t mbc1_low5, mbc1_high2, mbc1_mode;
    uint8_t mbc3_rom_bank, mbc3_ram_bank;
    char title[17];
} GB_Cartridge;

/* Returns false with a printable error in err on invalid/unsupported cartridge. */
bool gb_cart_load(GB_Cartridge *cart, const char *path, char *err, size_t err_size);
/* Same validation as load, but copies bytes from an in-memory ROM image. */
bool gb_cart_from_bytes(GB_Cartridge *cart, const uint8_t *rom, size_t size,
                        char *err, size_t err_size);
void gb_cart_free(GB_Cartridge *cart);
uint8_t gb_cart_read(const GB_Cartridge *cart, uint16_t addr);
void gb_cart_write(GB_Cartridge *cart, uint16_t addr, uint8_t value);
/* Optional cartridge battery RAM persistence. Returns false on I/O error. */
bool gb_cart_save_ram(const GB_Cartridge *cart, const char *path);
bool gb_cart_load_ram(GB_Cartridge *cart, const char *path);
/* Automatic saves use the ROM path with .sav replacing the final extension.
 * Loading succeeds if no save exists; malformed/existing saves fail.
 * No-op for cartridges without battery-backed RAM. */
bool gb_cart_auto_load_save(GB_Cartridge *cart, const char *rom_path);
bool gb_cart_auto_write_save(const GB_Cartridge *cart, const char *rom_path);
#endif
