#include "cartridge.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *err, size_t cap, const char *message) {
    if (err && cap) snprintf(err, cap, "%s", message);
    return false;
}
static unsigned rom_count(uint8_t code) {
    if (code <= 8) return 2u << code;
    if (code == 0x52) return 72;
    if (code == 0x53) return 80;
    if (code == 0x54) return 96;
    return 0;
}
static size_t ram_length(uint8_t code) {
    switch (code) {
        case 0: return 0;
        case 1: return 2048;
        case 2: return 8192;
        case 3: return 32768;
        case 4: return 131072;
        case 5: return 65536;
        default: return 0;
    }
}
void gb_cart_free(GB_Cartridge *c) {
    if (!c) return;
    free(c->rom);
    free(c->ram);
    memset(c, 0, sizeof(*c));
}
bool gb_cart_from_bytes(GB_Cartridge *c, const uint8_t *bytes, size_t size,
                        char *err, size_t cap) {
    if (!c) return fail(err, cap, "Null cartridge");
    /* Caller must free an existing cartridge before reloading. */
    memset(c, 0, sizeof(*c));
    if (!bytes || size < 0x150) return fail(err, cap, "ROM too small for cartridge header");
    uint8_t type = bytes[0x147], ram_code = bytes[0x149];
    unsigned banks = rom_count(bytes[0x148]);
    if (!banks) return fail(err, cap, "Unsupported ROM size code");
    if (type != 0x00 && type != 0x01 && type != 0x02 && type != 0x03 &&
        type != 0x08 && type != 0x09 && type != 0x11 && type != 0x12 && type != 0x13)
        return fail(err, cap, "Unsupported mapper (supports ROM-only, MBC1 and MBC3 without RTC)");
    if (size != (size_t)banks * 0x4000)
        return fail(err, cap, "ROM file length does not match header ROM size");
    if ((type == 0x01 || type == 0x02 || type == 0x03) && banks > 128)
        return fail(err, cap, "MBC1 cannot address this ROM size");
    bool ram_type = type == 0x02 || type == 0x03 || type == 0x08 || type == 0x09 ||
                    type == 0x12 || type == 0x13;
    if (ram_type && ram_code == 0) return fail(err, cap, "Cartridge declares RAM but RAM size is zero");
    if (!ram_type && ram_code != 0) return fail(err, cap, "RAM size without RAM cartridge type");
    size_t ram_size = ram_type ? ram_length(ram_code) : 0;
    if (ram_type && !ram_size) return fail(err, cap, "Invalid RAM size code");
    if (type >= 1 && type <= 3 && ram_size > 32768)
        return fail(err, cap, "MBC1 supports at most 32 KiB external RAM");
    if (type >= 0x11 && type <= 0x13 && banks > 128)
        return fail(err, cap, "MBC3 cannot address this ROM size");
    if (type >= 0x11 && type <= 0x13 && ram_size > 32768)
        return fail(err, cap, "MBC3 supports at most 32 KiB external RAM");
    c->rom = malloc(size);
    if (!c->rom) return fail(err, cap, "Out of memory allocating ROM");
    memcpy(c->rom, bytes, size);
    if (ram_size) {
        c->ram = calloc(ram_size, 1);
        if (!c->ram) { gb_cart_free(c); return fail(err, cap, "Out of memory allocating RAM"); }
    }
    c->rom_size = size; c->ram_size = ram_size;
    c->rom_banks = banks; c->ram_banks = ram_size ? (unsigned)((ram_size + 8191) / 8192) : 0;
    c->type = type;
    c->mapper = (type >= 1 && type <= 3) ? GB_MAPPER_MBC1 :
                (type >= 0x11 && type <= 0x13) ? GB_MAPPER_MBC3 : GB_MAPPER_ROM;
    c->has_battery = type == 0x03 || type == 0x09 || type == 0x13;
    c->ram_enabled = c->mapper == GB_MAPPER_ROM;
    c->mbc1_low5 = 1;
    c->mbc3_rom_bank = 1;
    for (int i = 0; i < 16; ++i) {
        uint8_t ch = bytes[0x134 + i];
        if (ch < 32 || ch > 126) break;
        c->title[i] = (char)ch;
    }
    return true;
}
bool gb_cart_load(GB_Cartridge *c, const char *path, char *err, size_t cap) {
    if (!c || !path) return fail(err, cap, "Null cartridge or filename");
    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, cap, "Cannot open ROM file");
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) { fclose(f); return fail(err, cap, "Cannot seek ROM file"); }
    long end = ftell(f);
    if (fseek(f, 0, SEEK_SET)) { fclose(f); return fail(err, cap, "Cannot rewind ROM file"); }
    uint8_t *buffer = malloc((size_t)end ? (size_t)end : 1);
    if (!buffer) { fclose(f); return fail(err, cap, "Out of memory reading ROM"); }
    bool ok = fread(buffer, 1, (size_t)end, f) == (size_t)end;
    fclose(f);
    if (!ok) { free(buffer); return fail(err, cap, "Failed to read ROM file"); }
    ok = gb_cart_from_bytes(c, buffer, (size_t)end, err, cap);
    free(buffer);
    return ok;
}
static unsigned mbc1_bank0(const GB_Cartridge *c) {
    return c->mbc1_mode ? ((unsigned)c->mbc1_high2 << 5) % c->rom_banks : 0;
}
static unsigned mbc1_bank1(const GB_Cartridge *c) {
    unsigned bank = ((unsigned)c->mbc1_high2 << 5) | c->mbc1_low5;
    return bank % c->rom_banks;
}
uint8_t gb_cart_read(const GB_Cartridge *c, uint16_t a) {
    if (!c || !c->rom) return 0xFF;
    if (a < 0x8000) {
        unsigned bank = 0;
        if (a < 0x4000) bank = c->mapper == GB_MAPPER_MBC1 ? mbc1_bank0(c) : 0;
        else bank = c->mapper == GB_MAPPER_MBC1 ? mbc1_bank1(c) :
                    c->mapper == GB_MAPPER_MBC3 ? c->mbc3_rom_bank % c->rom_banks : 1;
        size_t index = (size_t)bank * 0x4000 + (a & 0x3FFF);
        return index < c->rom_size ? c->rom[index] : 0xFF;
    }
    if (a >= 0xA000 && a < 0xC000 && c->ram && c->ram_enabled) {
        unsigned bank = c->mapper == GB_MAPPER_MBC1 && c->mbc1_mode ? c->mbc1_high2 :
                        c->mapper == GB_MAPPER_MBC3 ? c->mbc3_ram_bank : 0;
        size_t index = (size_t)bank * 0x2000 + (a - 0xA000);
        if (c->ram_size == 2048) index %= 2048;
        return index < c->ram_size ? c->ram[index] : 0xFF;
    }
    return 0xFF;
}
void gb_cart_write(GB_Cartridge *c, uint16_t a, uint8_t v) {
    if (!c || !c->rom) return;
    if (a < 0x8000 && c->mapper == GB_MAPPER_MBC1) {
        if (a < 0x2000) c->ram_enabled = (v & 0x0F) == 0x0A;
        else if (a < 0x4000) { c->mbc1_low5 = v & 0x1F; if (!c->mbc1_low5) c->mbc1_low5 = 1;
    c->mbc3_rom_bank = 1; }
        else if (a < 0x6000) c->mbc1_high2 = v & 3;
        else c->mbc1_mode = v & 1;
    } else if (a < 0x8000 && c->mapper == GB_MAPPER_MBC3) {
        if (a < 0x2000) c->ram_enabled = (v & 0x0F) == 0x0A;
        else if (a < 0x4000) {
            c->mbc3_rom_bank = v & 0x7F;
            if (!c->mbc3_rom_bank) c->mbc3_rom_bank = 1;
        } else if (a < 0x6000) {
            /* 0x08..0x0C select RTC registers on timer-equipped MBC3 carts.
             * This implementation deliberately supports only non-RTC types. */
            c->mbc3_ram_bank = v;
        } else {
            /* RTC latch writes are ignored on 0x11/0x12/0x13. */
        }
    } else if (a >= 0xA000 && a < 0xC000 && c->ram && c->ram_enabled) {
        unsigned bank = c->mapper == GB_MAPPER_MBC1 && c->mbc1_mode ? c->mbc1_high2 :
                        c->mapper == GB_MAPPER_MBC3 ? c->mbc3_ram_bank : 0;
        size_t index = (size_t)bank * 0x2000 + a - 0xA000;
        if (c->ram_size == 2048) index %= 2048;
        if (index < c->ram_size) c->ram[index] = v;
    }
}
bool gb_cart_save_ram(const GB_Cartridge *c, const char *path) {
    if (!c || !path || !c->ram) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(c->ram, 1, c->ram_size, f) == c->ram_size;
    if (fclose(f)) ok = false;
    return ok;
}
bool gb_cart_load_ram(GB_Cartridge *c, const char *path) {
    if (!c || !path || !c->ram) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(c->ram, 1, c->ram_size, f) == c->ram_size;
    if (fgetc(f) != EOF) ok = false;
    if (fclose(f)) ok = false;
    return ok;
}

/* Keep save files next to the ROM: example pokemon.gb -> pokemon.sav. */
static char *save_filename(const char *rom_path) {
    if (!rom_path) return NULL;
    size_t n = strlen(rom_path);
    const char *slash = strrchr(rom_path, '/');
    const char *backslash = strrchr(rom_path, '\\');
    const char *base = rom_path;
    if (slash && slash + 1 > base) base = slash + 1;
    if (backslash && backslash + 1 > base) base = backslash + 1;
    const char *dot = strrchr(base, '.');
    size_t prefix = (dot && dot != base) ? (size_t)(dot - rom_path) : n;
    char *result = malloc(prefix + 5);
    if (!result) return NULL;
    memcpy(result, rom_path, prefix);
    memcpy(result + prefix, ".sav", 5);
    return result;
}

bool gb_cart_auto_load_save(GB_Cartridge *c, const char *rom_path) {
    if (!c) return false;
    if (!c->has_battery || !c->ram_size) return true;
    char *path = save_filename(rom_path);
    if (!path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* A missing save is expected on first launch; other errors are not. */
        /* errno is captured immediately following fopen. */
        bool missing = errno == ENOENT;
        free(path);
        return missing;
    }
    fclose(f);
    bool ok = gb_cart_load_ram(c, path);
    free(path);
    return ok;
}

bool gb_cart_auto_write_save(const GB_Cartridge *c, const char *rom_path) {
    if (!c) return false;
    if (!c->has_battery || !c->ram_size) return true;
    char *path = save_filename(rom_path);
    if (!path) return false;
    bool ok = gb_cart_save_ram(c, path);
    free(path);
    return ok;
}
