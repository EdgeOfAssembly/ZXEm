#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include "z80.h"
#include "ula.h"
#include "snapshot.h"
#include "config.h"

static Z80 z80;
static ULA ula;
static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* texture = nullptr;
static SDL_AudioDeviceID audio_dev = 0;
static SDL_Joystick* joystick = nullptr;
static bool running = true;
static const int SAMPLE_RATE = 44100;
static const int SCALE = 3;

// Paths set from command line or defaults.
static std::string g_game_path;
static std::string g_rom_dir = "rom";
static std::string g_rom_path;
static std::string g_config_path = "config.ini";
static std::string g_model = "spectrum48";

// Audio ring buffer for beeper-driven output.
// The main thread writes samples as the beeper bit changes; the SDL audio callback reads them.
static const int AUDIO_BUFFER_SIZE = 16384;
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];
static volatile int audio_write_pos = 0;
static volatile int audio_read_pos = 0;
static int audio_callback_count = 0;

static void audio_callback(void* /*userdata*/, uint8_t* stream, int len) {
    int16_t* buf = (int16_t*)stream;
    int samples = len / 2;
    audio_callback_count += samples;
    for (int i = 0; i < samples; i++) {
        int rp = audio_read_pos;
        if (rp != audio_write_pos) {
            buf[i] = audio_buffer[rp];
            audio_read_pos = (rp + 1) % AUDIO_BUFFER_SIZE;
        } else {
            // underrun: repeat last known level or silence
            buf[i] = ula.beeper_state ? 5000 : -5000;
        }
    }
}

static void pushAudioSample(int16_t sample) {
    int wp = audio_write_pos;
    int next = (wp + 1) % AUDIO_BUFFER_SIZE;
    if (next == audio_read_pos) {
        // buffer full: drop oldest sample
        audio_read_pos = (audio_read_pos + 1) % AUDIO_BUFFER_SIZE;
    }
    audio_buffer[wp] = sample;
    audio_write_pos = next;
}

static void init_audio() {
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
}

static void updateAudio() {
    if (!ula.beeper_changed) return;
    ula.beeper_changed = false;
    int16_t sample = ula.beeper_state ? 5000 : -5000;
    pushAudioSample(sample);
}

static void handle_joy_button(int button, bool pressed) {
    // Common USB gamepad button layout; tweak to taste after testing
    switch (button) {
        case 0: ula.setKey(7, 0, pressed); break; // A/Cross = Jump (Space)
        case 1: ula.setKey(6, 0, pressed); break; // B/Circle = Start (Enter)
        case 6: ula.setKey(7, 0, pressed); break; // Select
        case 7: ula.setKey(6, 0, pressed); break; // Start
        default: break;
    }
}

static void handle_joy_axis(int axis, int16_t value) {
    // Deadzone 4096 (out of 32767)
    const int DEADZONE = 4096;
    if (axis == 0) {
        bool left = value < -DEADZONE;
        bool right = value > DEADZONE;
        // Kempston joystick: bit0=right, bit1=left
        uint8_t k = ula.kempston & ~0x03;
        if (right) k |= 0x01;
        if (left) k |= 0x02;
        ula.setKempston(k);
    }
}

static void handle_key(SDL_Keycode key, bool pressed) {
    switch (key) {
        case SDLK_ESCAPE:
            if (pressed) running = false;
            break;
        case SDLK_F1:
            if (pressed) {
                fprintf(stderr, "Resetting to initial snapshot state...\n");
                z80.reset();
                ula.reset();
                if (!loadZ80(g_game_path.c_str(), z80, ula)) {
                    fprintf(stderr, "Reset load failed\n");
                    running = false;
                }
            }
            break;
        case SDLK_F5:
            if (pressed) {
                const char* path = "/tmp/reverse/emulator/savestate.z80";
                if (saveZ80(path, z80, ula)) {
                    fprintf(stderr, "Saved state to %s\n", path);
                } else {
                    fprintf(stderr, "Failed to save state\n");
                }
            }
            break;
        case SDLK_F9:
            if (pressed) {
                const char* path = "/tmp/reverse/emulator/savestate.z80";
                if (saveZ80(path, z80, ula)) {
                    fprintf(stderr, "Quick-saved state to %s\n", path);
                }
            }
            break;
        case SDLK_F10:
            if (pressed) {
                const char* path = "/tmp/reverse/emulator/savestate.z80";
                if (loadZ80(path, z80, ula)) {
                    fprintf(stderr, "Quick-loaded state from %s\n", path);
                } else {
                    fprintf(stderr, "Failed to quick-load state\n");
                }
            }
            break;

        // Spectrum keyboard matrix rows
        case SDLK_LSHIFT: case SDLK_RSHIFT: ula.setKey(0, 0, pressed); break; // Shift
        case SDLK_z: ula.setKey(0, 1, pressed); break;
        case SDLK_x: ula.setKey(0, 2, pressed); break;
        case SDLK_c: ula.setKey(0, 3, pressed); break;
        case SDLK_v: ula.setKey(0, 4, pressed); break;

        case SDLK_a: ula.setKey(1, 0, pressed); break;
        case SDLK_s: ula.setKey(1, 1, pressed); break;
        case SDLK_d: ula.setKey(1, 2, pressed); break;
        case SDLK_f: ula.setKey(1, 3, pressed); break;
        case SDLK_g: ula.setKey(1, 4, pressed); break;

        case SDLK_q: ula.setKey(2, 0, pressed); break;
        case SDLK_w: ula.setKey(2, 1, pressed); break;
        case SDLK_e: ula.setKey(2, 2, pressed); break;
        case SDLK_r: ula.setKey(2, 3, pressed); break;
        case SDLK_t: ula.setKey(2, 4, pressed); break;

        case SDLK_1: ula.setKey(3, 0, pressed); break;
        case SDLK_2: ula.setKey(3, 1, pressed); break;
        case SDLK_3: ula.setKey(3, 2, pressed); break;
        case SDLK_4: ula.setKey(3, 3, pressed); break;
        case SDLK_5: ula.setKey(3, 4, pressed); break;

        case SDLK_0: ula.setKey(4, 0, pressed); break;
        case SDLK_9: ula.setKey(4, 1, pressed); break;
        case SDLK_8: ula.setKey(4, 2, pressed); break;
        case SDLK_7: ula.setKey(4, 3, pressed); break;
        case SDLK_6: ula.setKey(4, 4, pressed); break;

        case SDLK_p: ula.setKey(5, 0, pressed); break;
        case SDLK_o: ula.setKey(5, 1, pressed); break;
        case SDLK_i: ula.setKey(5, 2, pressed); break;
        case SDLK_u: ula.setKey(5, 3, pressed); break;
        case SDLK_y: ula.setKey(5, 4, pressed); break;

        case SDLK_RETURN: ula.setKey(6, 0, pressed); break; // Enter
        case SDLK_l: ula.setKey(6, 1, pressed); break;
        case SDLK_k: ula.setKey(6, 2, pressed); break;
        case SDLK_j: ula.setKey(6, 3, pressed); break;
        case SDLK_h: ula.setKey(6, 4, pressed); break;

        case SDLK_SPACE: ula.setKey(7, 0, pressed); break; // Space / Jump
        case SDLK_PERIOD: ula.setKey(7, 1, pressed); break; // .
        case SDLK_m: ula.setKey(7, 2, pressed); break;
        case SDLK_n: ula.setKey(7, 3, pressed); break;
        case SDLK_b: ula.setKey(7, 4, pressed); break;

        // Kempston joystick via arrow keys + right alt
        case SDLK_LEFT:   ula.setKempston(pressed ? (ula.kempston | 0x02) : (ula.kempston & ~0x02)); break;
        case SDLK_RIGHT:  ula.setKempston(pressed ? (ula.kempston | 0x01) : (ula.kempston & ~0x01)); break;
        case SDLK_UP:     ula.setKempston(pressed ? (ula.kempston | 0x08) : (ula.kempston & ~0x08)); break;
        case SDLK_DOWN:   ula.setKempston(pressed ? (ula.kempston | 0x04) : (ula.kempston & ~0x04)); break;
        case SDLK_RALT: case SDLK_LALT: ula.setKempston(pressed ? (ula.kempston | 0x10) : (ula.kempston & ~0x10)); break;

        default: break;
    }
}

static void print_usage(const char* argv0) {
    fprintf(stderr, "Usage: %s [options] [game.z80]\n", argv0);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --config FILE     Load configuration from FILE (default: ./config.ini)\n");
    fprintf(stderr, "  --model MODEL     Machine model (default: spectrum48)\n");
    fprintf(stderr, "  --rom FILE        Load a 16KB Spectrum 48K ROM image from FILE\n");
    fprintf(stderr, "  --rom-dir DIR     Look for a ROM in DIR (default: ./rom)\n");
    fprintf(stderr, "  --help            Show this help\n");
    fprintf(stderr, "\nSupported models: spectrum48\n");
    fprintf(stderr, "If no ROM is provided, a minimal synthetic ROM is generated automatically.\n");
    fprintf(stderr, "This synthetic ROM contains no copyrighted Sinclair code and is sufficient\n");
    fprintf(stderr, "for self-contained games like Manic Miner.\n");
}

int main(int argc, char* argv[]) {
    // Default paths.
    g_game_path = "/tmp/reverse/Manic_Miner_1983_Bug_Byte_Software.z80";

    // 1. Load config.ini if present. CLI arguments will override it later.
    Config cfg;
    bool config_loaded = cfg.load(g_config_path.c_str());
    if (!config_loaded) {
        // Try loading from XDG config dir if not in current dir
        const char* xdg = getenv("XDG_CONFIG_HOME");
        std::string alt;
        if (xdg) {
            alt = std::string(xdg) + "/zxem/config.ini";
        } else {
            const char* home = getenv("HOME");
            if (home) alt = std::string(home) + "/.config/zxem/config.ini";
        }
        if (!alt.empty()) config_loaded = cfg.load(alt.c_str());
    }
    if (config_loaded) {
        if (cfg.has("emulator", "game"))  g_game_path = cfg.getString("emulator", "game", g_game_path);
        if (cfg.has("emulator", "model")) g_model    = cfg.getString("emulator", "model", g_model);
        if (cfg.has("rom", "file"))       g_rom_path = cfg.getString("rom", "file", g_rom_path);
        if (cfg.has("rom", "dir"))        g_rom_dir  = cfg.getString("rom", "dir", g_rom_dir);
        fprintf(stderr, "Loaded config from %s: model=%s game=%s rom=%s rom_dir=%s\n",
                g_config_path.c_str(), g_model.c_str(), g_game_path.c_str(), g_rom_path.c_str(), g_rom_dir.c_str());
    }

    // 2. Command-line overrides.
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Missing argument for --config\n"); return 1; }
            g_config_path = argv[++i];
            // Reload from the new path, overriding any previous config
            if (cfg.load(g_config_path.c_str())) {
                if (cfg.has("emulator", "game"))  g_game_path = cfg.getString("emulator", "game", g_game_path);
                if (cfg.has("emulator", "model")) g_model    = cfg.getString("emulator", "model", g_model);
                if (cfg.has("rom", "file"))       g_rom_path = cfg.getString("rom", "file", g_rom_path);
                if (cfg.has("rom", "dir"))        g_rom_dir  = cfg.getString("rom", "dir", g_rom_dir);
                fprintf(stderr, "Loaded config from %s: model=%s game=%s rom=%s rom_dir=%s\n",
                        g_config_path.c_str(), g_model.c_str(), g_game_path.c_str(), g_rom_path.c_str(), g_rom_dir.c_str());
            }
        } else if (strcmp(argv[i], "--rom") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Missing argument for --rom\n"); return 1; }
            g_rom_path = argv[++i];
        } else if (strcmp(argv[i], "--rom-dir") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Missing argument for --rom-dir\n"); return 1; }
            g_rom_dir = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Missing argument for --model\n"); return 1; }
            g_model = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            g_game_path = argv[i];
        }
        i++;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("ZX Spectrum 48K Emulator",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              ULA::SCREEN_WIDTH * SCALE, ULA::SCREEN_HEIGHT * SCALE,
                              SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                ULA::SCREEN_WIDTH, ULA::SCREEN_HEIGHT);
    if (!texture) {
        fprintf(stderr, "Texture creation failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    init_audio();

    if (SDL_NumJoysticks() > 0) {
        joystick = SDL_JoystickOpen(0);
        if (joystick) {
            fprintf(stderr, "Opened joystick: %s\n", SDL_JoystickName(joystick));
        }
    }

    z80.ula = &ula;

    // Validate machine model (only spectrum48 is currently supported)
    {
        std::string m = g_model;
        for (char& c : m) c = std::tolower(static_cast<unsigned char>(c));
        if (m != "spectrum48" && m != "48" && m != "spec48") {
            fprintf(stderr, "Error: unsupported model '%s'. Only 'spectrum48' is currently supported.\n", g_model.c_str());
            SDL_Quit();
            return 1;
        }
        g_model = "spectrum48";
    }

    // ROM loading priority:
    // 1. Explicit --rom file
    // 2. Any 16384-byte file in --rom-dir (default ./rom)
    // 3. Minimal synthetic ROM (legally clean, no copyrighted code)
    bool rom_loaded = false;
    if (!g_rom_path.empty()) {
        rom_loaded = loadROM(g_rom_path.c_str(), ula);
        if (!rom_loaded) {
            fprintf(stderr, "Failed to load explicit ROM %s; falling back to auto-detection\n", g_rom_path.c_str());
        }
    }
    if (!rom_loaded) {
        rom_loaded = loadROMFromDir(g_rom_dir.c_str(), ula);
    }
    if (!rom_loaded) {
        ula.resetToSyntheticROM();
        fprintf(stderr, "No ROM found: using legally clean synthetic ROM.\n");
    }

    if (!loadZ80(g_game_path.c_str(), z80, ula)) {
        fprintf(stderr, "Failed to load Z80 snapshot: %s\n", g_game_path.c_str());
        SDL_Quit();
        return 1;
    }

    printf("Z80 loaded. PC=0x%04X SP=0x%04X A=0x%02X F=0x%02X\n", z80.PC, z80.SP, z80.A, z80.F);
    printf("HL=0x%04X DE=0x%04X BC=0x%04X IX=0x%04X IY=0x%04X\n",
           z80.getHL(), z80.getDE(), z80.getBC(), z80.IX, z80.IY);
    printf("IM=%d IFF1=%d\n", z80.IM, z80.IFF1);

    const int TSTATES_PER_FRAME = 69888;
    const int TARGET_FRAME_US = 20000;
    uint64_t last_frame_time = SDL_GetPerformanceCounter();
    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    int frame_count = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (!event.key.repeat) handle_key(event.key.keysym.sym, true);
                    break;
                case SDL_KEYUP:
                    handle_key(event.key.keysym.sym, false);
                    break;
                case SDL_JOYBUTTONDOWN:
                    handle_joy_button(event.jbutton.button, true);
                    break;
                case SDL_JOYBUTTONUP:
                    handle_joy_button(event.jbutton.button, false);
                    break;
                case SDL_JOYAXISMOTION:
                    handle_joy_axis(event.jaxis.axis, event.jaxis.value);
                    break;
            }
        }

        int tstates_this_frame = 0;
        while (tstates_this_frame < TSTATES_PER_FRAME) {
            int ts = z80.execute();
            ula.step(ts);
            updateAudio();
            tstates_this_frame += ts;

            if (ula.frame_tstates >= TSTATES_PER_FRAME) {
                ula.frame_tstates -= TSTATES_PER_FRAME;
                // Generate IM1 interrupt at the start of each new frame
                if (z80.IFF1) {
                    z80.IFF1 = z80.IFF2 = false;
                    z80.halted = false;
                    ula.step(7); // RST 7 tstates for interrupt ack + push PC
                    z80.SP -= 2;
                    ula.write(z80.SP, z80.PC & 0xFF);
                    ula.write(z80.SP + 1, z80.PC >> 8);
                    z80.PC = 0x0038;
                    tstates_this_frame += 7;
                }
            }
        }

        uint32_t pixels[ULA::SCREEN_WIDTH * ULA::SCREEN_HEIGHT];
        ula.renderFrame(pixels, ULA::SCREEN_WIDTH * 4);
        SDL_UpdateTexture(texture, nullptr, pixels, ULA::SCREEN_WIDTH * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        frame_count++;
        if (frame_count % 50 == 0) {
            printf("Frame %d, PC=0x%04X SP=0x%04X\n", frame_count, z80.PC, z80.SP);
        }

        uint64_t now = SDL_GetPerformanceCounter();
        uint64_t elapsed_us = (now - last_frame_time) * 1000000 / perf_freq;
        if (elapsed_us < TARGET_FRAME_US) {
            SDL_Delay((TARGET_FRAME_US - elapsed_us) / 1000);
        }
        last_frame_time = SDL_GetPerformanceCounter();
    }

    if (joystick) SDL_JoystickClose(joystick);
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
