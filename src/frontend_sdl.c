/* SDL2 frontend for the DMG core. All CPU/PPU/timer state lives in the core. */
#include "bus.h"
#include "cpu.h"
#include <SDL.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GB_CLOCK_HZ 4194304u
#define GB_CYCLES_PER_FRAME 70224u

static const uint32_t dmg_colors[4] = {
    0xFFE0F8D0u, 0xFF88C070u, 0xFF346856u, 0xFF081820u
};

static uint8_t key_bit(SDL_Keycode key) {
    switch (key) {
        case SDLK_RIGHT: return 0x01;
        case SDLK_LEFT: return 0x02;
        case SDLK_UP: return 0x04;
        case SDLK_DOWN: return 0x08;
        case SDLK_z: return 0x10;     /* A */
        case SDLK_x: return 0x20;     /* B */
        case SDLK_BACKSPACE: return 0x40; /* Select */
        case SDLK_RETURN: return 0x80;    /* Start */
        default: return 0;
    }
}

static void refresh_keyboard(GB_Bus *bus) {
    /* Rebuild from physical keyboard to prevent stuck keys on focus loss. */
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t buttons = 0;
    if (keys[SDL_SCANCODE_RIGHT]) buttons |= 0x01;
    if (keys[SDL_SCANCODE_LEFT]) buttons |= 0x02;
    if (keys[SDL_SCANCODE_UP]) buttons |= 0x04;
    if (keys[SDL_SCANCODE_DOWN]) buttons |= 0x08;
    if (keys[SDL_SCANCODE_Z]) buttons |= 0x10;
    if (keys[SDL_SCANCODE_X]) buttons |= 0x20;
    if (keys[SDL_SCANCODE_BACKSPACE]) buttons |= 0x40;
    if (keys[SDL_SCANCODE_RETURN]) buttons |= 0x80;
    gb_bus_set_buttons(bus, buttons);
}

static void paint_frame(SDL_Texture *texture, const GB_PPU *ppu) {
    uint32_t pixels[GB_LCD_WIDTH * GB_LCD_HEIGHT];
    for (unsigned y = 0; y < GB_LCD_HEIGHT; ++y)
        for (unsigned x = 0; x < GB_LCD_WIDTH; ++x)
            pixels[y * GB_LCD_WIDTH + x] = dmg_colors[ppu->framebuffer[y][x] & 3];
    SDL_UpdateTexture(texture, NULL, pixels, GB_LCD_WIDTH * (int)sizeof(uint32_t));
}

static void usage(const char *executable) {
    fprintf(stderr, "Usage: %s path/to/game.gb [scale=4]\n", executable);
    fprintf(stderr, "Keys: arrows=D-pad, Z=A, X=B, Enter=Start, Backspace=Select\n");
    fprintf(stderr, "      Space=pause, Tab=fast-forward, Esc=quit\n");
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) { usage(argv[0]); return 1; }
    int scale = 4;
    if (argc == 3) {
        char *end = NULL;
        long requested = strtol(argv[2], &end, 10);
        if (!end || *end || requested < 1 || requested > 10) {
            fprintf(stderr, "Scale must be between 1 and 10.\n");
            return 1;
        }
        scale = (int)requested;
    }

    GB_Bus *bus = malloc(sizeof(*bus));
    if (!bus) { fprintf(stderr, "Out of memory\n"); return 1; }
    gb_bus_init(bus, true);
    char error[128];
    if (!gb_cart_load(&bus->cart, argv[1], error, sizeof(error))) {
        fprintf(stderr, "ROM error: %s\n", error);
        free(bus);
        return 1;
    }
    if (!gb_cart_auto_load_save(&bus->cart, argv[1])) {
        fprintf(stderr, "Warning: could not load existing battery save (.sav).\n");
    }
    GB_CPU cpu;
    gb_cpu_init(&cpu, bus, gb_bus_read, gb_bus_write, true);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        gb_cart_free(&bus->cart); free(bus); return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Game Boy Emulator", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, GB_LCD_WIDTH * scale, GB_LCD_HEIGHT * scale,
        SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, GB_LCD_WIDTH, GB_LCD_HEIGHT) : NULL;
    if (!window || !renderer || !texture) {
        fprintf(stderr, "SDL window/renderer/texture error: %s\n", SDL_GetError());
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); gb_cart_free(&bus->cart); free(bus); return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(renderer, GB_LCD_WIDTH, GB_LCD_HEIGHT);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);

    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    wanted.freq = (int)GB_APU_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 1024;
    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
    if (!audio_device) fprintf(stderr, "Warning: audio unavailable: %s\n", SDL_GetError());
    else SDL_PauseAudioDevice(audio_device, 0);
    bool muted = false;
    int16_t audio_batch[GB_APU_BUFFER_FRAMES * 2];

    printf("Loaded %s | type 0x%02X | %u ROM banks\n", bus->cart.title,
           bus->cart.type, bus->cart.rom_banks);
    puts("Arrows=move, Z=A, X=B, Enter=Start, Backspace=Select, Space=pause, Tab=fast, M=mute, Esc=quit");

    bool running = true, paused = false, fast_forward = false;
    /* Increment by a precise 70224-cycle frame at approximately 59.7275 Hz.
     * A real-time accumulator avoids high CPU use and busy waiting.
     * Bound catch-up work after long stalls (window dragging, debugging). */
    const double seconds_per_frame = (double)GB_CYCLES_PER_FRAME / GB_CLOCK_HZ;
    const uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t previous = SDL_GetPerformanceCounter();
    double accumulated = 0.0;
    uint64_t generated_frames = 0;

    paint_frame(texture, &bus->ppu);
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) running = false;
                else if (key == SDLK_SPACE) {
                    paused = !paused;
                    accumulated = 0.0;
                    if (audio_device) SDL_ClearQueuedAudio(audio_device);
                    gb_apu_clear_samples(&bus->apu);
                }
                else if (key == SDLK_m) {
                    muted = !muted;
                    if (audio_device && muted) SDL_ClearQueuedAudio(audio_device);
                }
                else if (key == SDLK_TAB) fast_forward = true;
                else if (key_bit(key)) refresh_keyboard(bus);
            } else if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_TAB) fast_forward = false;
                refresh_keyboard(bus);
            } else if (event.type == SDL_WINDOWEVENT &&
                      event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                gb_bus_set_buttons(bus, 0);
                fast_forward = false;
            }
        }
        refresh_keyboard(bus);

        uint64_t now = SDL_GetPerformanceCounter();
        double delta = (double)(now - previous) / (double)perf_freq;
        previous = now;
        if (delta > 0.1) delta = 0.1;
        if (!paused && running) accumulated += delta;
        /* Fast-forward: process additional emulated frames without waiting. */
        if (!paused && running && fast_forward) accumulated += 4.0 * seconds_per_frame;
        if (accumulated > seconds_per_frame * 5.0) accumulated = seconds_per_frame * 5.0;

        unsigned frames_this_iteration = 0;
        while (running && !paused && accumulated >= seconds_per_frame &&
               frames_this_iteration < 5) {
            /* Run until the next VBlank, rather than assuming an instruction
             * lands exactly on the frame boundary. */
            uint64_t start_frame = bus->ppu.frames;
            unsigned steps = 0;
            while (bus->ppu.frames == start_frame) {
                unsigned cycles = gb_cpu_step(&cpu);
                if (!cycles || cpu.stopped) {
                    fprintf(stderr, "CPU stopped at PC=%04X (opcode=%02X)\n",
                            cpu.pc, cpu.last_opcode);
                    running = false;
                    break;
                }
                gb_bus_tick(bus, cycles);
                if (++steps >= 500000) {
                    fprintf(stderr, "No VBlank generated (LCD may be disabled); stopping.\n");
                    running = false;
                    break;
                }
            }
            if (!running) break;
            /* Produce one stereo batch per emulated video frame. Cap queued
             * output latency: do not let backlog grow during fast-forward. */
            size_t frame_count = gb_apu_take_samples(&bus->apu, audio_batch, GB_APU_BUFFER_FRAMES);
            if (audio_device && !muted && !fast_forward && frame_count) {
                if (SDL_GetQueuedAudioSize(audio_device) > GB_APU_RATE * 2u * sizeof(int16_t) / 10u)
                    SDL_ClearQueuedAudio(audio_device);
                if (SDL_QueueAudio(audio_device, audio_batch,
                        (Uint32)(frame_count * 2 * sizeof(int16_t))) != 0)
                    fprintf(stderr, "Audio queue error: %s\n", SDL_GetError());
            } else if (audio_device && fast_forward) SDL_ClearQueuedAudio(audio_device);
            ++generated_frames;
            ++frames_this_iteration;
            accumulated -= seconds_per_frame;
            bus->ppu.frame_ready = false;
            paint_frame(texture, &bus->ppu);
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        /* Avoid burning CPU at full speed if vsync is unavailable. */
        if (frames_this_iteration == 0 && !fast_forward) SDL_Delay(1);
    }

    printf("Frames: %" PRIu64 " | CPU cycles: %" PRIu64 "\n", generated_frames, cpu.cycles);
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!gb_cart_auto_write_save(&bus->cart, argv[1]))
        fprintf(stderr, "Warning: could not write battery save (.sav).\n");
    gb_cart_free(&bus->cart);
    free(bus);
    return cpu.invalid_opcode ? 1 : 0;
}
