/**
 * @file main.cpp
 * @brief ZXEm SDL2 front-end and command-line interface.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <atomic>
#include <csignal>
#include <sys/stat.h>
#include <vector>

#include "ay.h"
#include "config.h"
#include "log.h"
#include "media.h"
#include "snapshot.h"
#include "ula.h"
#include "version.h"
#include "vfs.h"
#include "z80.h"

#if defined(__SANITIZE_ADDRESS__)
extern "C" const char* __lsan_default_suppressions(void)
{
    /* SDL audio (Pulse/PipeWire) keeps dbus allocations until process exit. */
    return
        "leak:libdbus-\n"
        "leak:libpulse\n"
        "leak:libpulsecommon\n"
        "leak:libsystemd.so\n"
        "leak:libpipewire-\n"
        "leak:libglib-2.0.so\n"
        "leak:libgio-2.0.so\n"
        "leak:libSDL2\n"
        "leak:libzip.so\n"
        "leak:libz.so\n";
}
#endif

static Z80 z80;
static ULA ula;
static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* texture = nullptr;
static SDL_AudioDeviceID audio_dev = 0;
static SDL_Joystick* joystick = nullptr;
static bool running = true;
/** @brief 44100 samples per ula.cpu_hz() T-states (48K 3.5e6 / 128K 3.5469e6), not a fixed 79 T/sample. */
static const int SAMPLE_RATE = 44100;
static int g_scale = 3;

static std::string g_game_path;
static std::string g_rom_dir = "rom";
static std::string g_rom_path;
static std::string g_trdos_rom_path;
static std::string g_plus3_rom_path;
static std::string g_config_path = "config.ini";
static std::string g_model = "spectrum48";
static std::string g_keymap = "spectrum";
/** @brief If set, write a dirty TRD/EDSK image here on exit (default: discard). */
static std::string g_disk_out;
/** @brief 48K ULA issue: "2" or "3" (default 3). 128K/+3 always read as 3. */
static std::string g_issue = "3";
static bool g_no_system_rom = false;
static volatile sig_atomic_t g_exit_req = 0;

extern "C" {
static void zxem_on_signal(int)
{
    g_exit_req = 1;
}
}
static std::string g_member;
static std::string g_pok_path;
static std::string g_log_file;

static bool g_headless = false;
static bool g_no_audio = false;
static bool g_list_only = false;
static bool g_heartbeat = false;
static int g_max_frames = -1;

static const int AUDIO_BUFFER_SIZE = 32768;
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];
static std::atomic<int> audio_write_pos{0};
static std::atomic<int> audio_read_pos{0};
static std::atomic<int16_t> audio_last_sample{0};

static void audio_callback(void*, uint8_t* stream, int len)
{
    int16_t* buf = reinterpret_cast<int16_t*>(stream);
    const int samples = len / 2;
    for (int i = 0; i < samples; i++)
    {
        int rp = audio_read_pos.load(std::memory_order_relaxed);
        const int wp = audio_write_pos.load(std::memory_order_acquire);
        if (rp != wp)
        {
            const int16_t s = audio_buffer[rp];
            buf[i] = s;
            audio_last_sample.store(s, std::memory_order_relaxed);
            audio_read_pos.store((rp + 1) % AUDIO_BUFFER_SIZE, std::memory_order_release);
        }
        else
        {
            buf[i] = audio_last_sample.load(std::memory_order_relaxed);
        }
    }
}

static void pushAudioSample(int16_t sample)
{
    const int wp = audio_write_pos.load(std::memory_order_relaxed);
    const int next = (wp + 1) % AUDIO_BUFFER_SIZE;
    const int rp = audio_read_pos.load(std::memory_order_acquire);
    if (next == rp)
    {
        /* Drop newest; the producer must not move read_pos. */
        return;
    }
    audio_buffer[wp] = sample;
    audio_write_pos.store(next, std::memory_order_release);
}

static void init_audio()
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_dev)
    {
        SDL_PauseAudioDevice(audio_dev, 0);
    }
}

/** @brief Bresenham T-state accumulator: emit SAMPLE_RATE samples per ula.cpu_hz() T-states (48K 3.5e6 / 128K 3.5469e6), not a fixed 79 T/sample. */
static int64_t audio_t_accum = 0;
static float audio_dc_x1 = 0.0f;
static float audio_dc_y1 = 0.0f;

static void updateAudio(int tstates)
{
    if (tstates <= 0)
    {
        return;
    }
    const int hz = ula.cpu_hz();
    audio_t_accum += static_cast<int64_t>(tstates) * SAMPLE_RATE;
    while (audio_t_accum >= hz)
    {
        audio_t_accum -= hz;
        const float x = ula.currentAudioSample();
        const float y = x - audio_dc_x1 + 0.995f * audio_dc_y1;
        audio_dc_x1 = x;
        audio_dc_y1 = y;
        pushAudioSample(static_cast<int16_t>(y * 12000.0f));
    }
}

static uint8_t g_joy_pad = 0;  /* Kempston fire (buttons) */
static uint8_t g_joy_axis = 0; /* analog stick direction bits */
static uint8_t g_joy_hat = 0;  /* hat direction bits */

static void handle_joy_button(int button, bool pressed)
{
    switch (button)
    {
        case 0:
        case 1:
        case 6:
        case 7:
            if (pressed)
            {
                g_joy_pad = static_cast<uint8_t>(g_joy_pad | 0x10);
            }
            else
            {
                g_joy_pad = static_cast<uint8_t>(g_joy_pad & ~0x10);
            }
            break;
        default:
            break;
    }
}

static void handle_joy_axis(int axis, int16_t value)
{
    const int DEADZONE = 4096;
    if (axis == 0)
    {
        g_joy_axis = static_cast<uint8_t>(g_joy_axis & ~0x03);
        if (value > DEADZONE)
        {
            g_joy_axis = static_cast<uint8_t>(g_joy_axis | 0x01);
        }
        else if (value < -DEADZONE)
        {
            g_joy_axis = static_cast<uint8_t>(g_joy_axis | 0x02);
        }
    }
    else if (axis == 1)
    {
        g_joy_axis = static_cast<uint8_t>(g_joy_axis & ~0x0C);
        if (value > DEADZONE)
        {
            g_joy_axis = static_cast<uint8_t>(g_joy_axis | 0x04);
        }
        else if (value < -DEADZONE)
        {
            g_joy_axis = static_cast<uint8_t>(g_joy_axis | 0x08);
        }
    }
}

/**
 * @brief Map an SDL hat value onto Kempston direction bits in g_joy_hat.
 * @param value SDL_HAT_* bitmask (centered clears the four direction bits).
 */
static void handle_joy_hat(uint8_t value)
{
    g_joy_hat = 0;
    if ((value & SDL_HAT_RIGHT) != 0)
    {
        g_joy_hat = static_cast<uint8_t>(g_joy_hat | 0x01);
    }
    if ((value & SDL_HAT_LEFT) != 0)
    {
        g_joy_hat = static_cast<uint8_t>(g_joy_hat | 0x02);
    }
    if ((value & SDL_HAT_DOWN) != 0)
    {
        g_joy_hat = static_cast<uint8_t>(g_joy_hat | 0x04);
    }
    if ((value & SDL_HAT_UP) != 0)
    {
        g_joy_hat = static_cast<uint8_t>(g_joy_hat | 0x08);
    }
}

static bool load_game_spec(const std::string& spec)
{
    VfsBlob blob;
    if (!vfs_read(spec, blob))
    {
        return false;
    }
    if (!media_load(blob, z80, ula))
    {
        return false;
    }
    if (!g_pok_path.empty())
    {
        VfsBlob pok;
        if (vfs_read(g_pok_path, pok))
        {
            media_apply_pok(pok, ula);
        }
        else
        {
            log_warn("could not read --pok %s", g_pok_path.c_str());
        }
    }
    return true;
}

/** @brief KEYDOWN latches until the end of this ULA frame (same-poll KEYUP). */
static bool g_key_latch[SDL_NUM_SCANCODES];

static bool key_down(const Uint8* ks, SDL_Scancode sc)
{
    const int i = static_cast<int>(sc);
    return ks[sc] || (i >= 0 && i < SDL_NUM_SCANCODES && g_key_latch[i]);
}

static void latch_scancode(SDL_Scancode sc)
{
    const int i = static_cast<int>(sc);
    if (i >= 0 && i < SDL_NUM_SCANCODES)
    {
        g_key_latch[i] = true;
    }
}

static void end_ula_frame_latches()
{
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < SDL_NUM_SCANCODES; i++)
    {
        if (g_key_latch[i] && ks[i] == 0)
        {
            g_key_latch[i] = false;
        }
    }
}

static void handle_key(SDL_Keycode key, bool pressed)
{
    switch (key)
    {
        case SDLK_ESCAPE:
            if (pressed)
            {
                running = false;
            }
            break;
        case SDLK_F1:
            if (pressed && !g_game_path.empty())
            {
                log_info("reset: reload %s", g_game_path.c_str());
                z80.reset();
                ula.reset();
                if (!load_game_spec(g_game_path))
                {
                    log_error("reset load failed");
                    running = false;
                }
            }
            break;
        case SDLK_F5:
            if (pressed)
            {
                const char* path = "savestate.z80";
                if (save_z80(path, z80, ula))
                {
                    log_info("saved state %s", path);
                }
                else
                {
                    log_error("save state failed");
                }
            }
            break;
        case SDLK_F9:
        case SDLK_F10:
            if (pressed)
            {
                const char* path = "savestate.z80";
                VfsBlob blob;
                if (vfs_read(path, blob) && media_load(blob, z80, ula))
                {
                    log_info("loaded state %s", path);
                }
                else
                {
                    log_error("quick-load failed");
                }
            }
            break;

        case SDLK_LSHIFT:
        case SDLK_RSHIFT: ula.setKey(0, 0, pressed); break;
        case SDLK_z: ula.setKey(0, 1, pressed); break;
        case SDLK_x: ula.setKey(0, 2, pressed); break;
        case SDLK_c: ula.setKey(0, 3, pressed); break;
        case SDLK_v: ula.setKey(0, 4, pressed); break;

        case SDLK_a:
            if (g_keymap != "wasd")
            {
                ula.setKey(1, 0, pressed);
            }
            break;
        case SDLK_s:
            if (g_keymap != "wasd")
            {
                ula.setKey(1, 1, pressed);
            }
            break;
        case SDLK_d:
            if (g_keymap != "wasd")
            {
                ula.setKey(1, 2, pressed);
            }
            break;
        case SDLK_f: ula.setKey(1, 3, pressed); break;
        case SDLK_g: ula.setKey(1, 4, pressed); break;

        case SDLK_q: ula.setKey(2, 0, pressed); break;
        case SDLK_w:
            if (g_keymap != "wasd")
            {
                ula.setKey(2, 1, pressed);
            }
            break;
        case SDLK_e: ula.setKey(2, 2, pressed); break;
        case SDLK_r: ula.setKey(2, 3, pressed); break;
        case SDLK_t: ula.setKey(2, 4, pressed); break;

        case SDLK_1:
        case SDLK_KP_1: ula.setKey(3, 0, pressed); break;
        case SDLK_2:
        case SDLK_KP_2: ula.setKey(3, 1, pressed); break;
        case SDLK_3:
        case SDLK_KP_3: ula.setKey(3, 2, pressed); break;
        case SDLK_4:
        case SDLK_KP_4: ula.setKey(3, 3, pressed); break;
        case SDLK_5:
        case SDLK_KP_5: ula.setKey(3, 4, pressed); break;

        case SDLK_0:
        case SDLK_KP_0: ula.setKey(4, 0, pressed); break;
        case SDLK_9:
        case SDLK_KP_9: ula.setKey(4, 1, pressed); break;
        case SDLK_8:
        case SDLK_KP_8: ula.setKey(4, 2, pressed); break;
        case SDLK_7:
        case SDLK_KP_7: ula.setKey(4, 3, pressed); break;
        case SDLK_6:
        case SDLK_KP_6: ula.setKey(4, 4, pressed); break;

        case SDLK_p: ula.setKey(5, 0, pressed); break;
        case SDLK_o: ula.setKey(5, 1, pressed); break;
        case SDLK_i: ula.setKey(5, 2, pressed); break;
        case SDLK_u: ula.setKey(5, 3, pressed); break;
        case SDLK_y: ula.setKey(5, 4, pressed); break;

        case SDLK_RETURN: ula.setKey(6, 0, pressed); break;
        case SDLK_l: ula.setKey(6, 1, pressed); break;
        case SDLK_k: ula.setKey(6, 2, pressed); break;
        case SDLK_j: ula.setKey(6, 3, pressed); break;
        case SDLK_h: ula.setKey(6, 4, pressed); break;

        case SDLK_SPACE: ula.setKey(7, 0, pressed); break;
        case SDLK_PERIOD: ula.setKey(7, 1, pressed); break;
        case SDLK_m: ula.setKey(7, 2, pressed); break;
        case SDLK_n: ula.setKey(7, 3, pressed); break;
        case SDLK_b: ula.setKey(7, 4, pressed); break;

        case SDLK_LEFT:
            ula.setKempston(pressed ? static_cast<uint8_t>(ula.kempston | 0x02)
                                    : static_cast<uint8_t>(ula.kempston & ~0x02));
            break;
        case SDLK_RIGHT:
            ula.setKempston(pressed ? static_cast<uint8_t>(ula.kempston | 0x01)
                                    : static_cast<uint8_t>(ula.kempston & ~0x01));
            break;
        case SDLK_UP:
            ula.setKempston(pressed ? static_cast<uint8_t>(ula.kempston | 0x08)
                                    : static_cast<uint8_t>(ula.kempston & ~0x08));
            break;
        case SDLK_DOWN:
            ula.setKempston(pressed ? static_cast<uint8_t>(ula.kempston | 0x04)
                                    : static_cast<uint8_t>(ula.kempston & ~0x04));
            break;
        case SDLK_RALT:
        case SDLK_LALT:
            ula.setKempston(pressed ? static_cast<uint8_t>(ula.kempston | 0x10)
                                    : static_cast<uint8_t>(ula.kempston & ~0x10));
            break;
        default:
            break;
    }
}

/**
 * @brief OR scancodes into the matrix. Does not clear event/sticky presses.
 */
static void apply_spectrum_keys()
{
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    std::memset(ula.keyboard, 0xFF, sizeof(ula.keyboard));

    struct Map
    {
        SDL_Scancode sc;
        uint8_t row;
        uint8_t bit;
    };
    static const Map kmap[] = {
        {SDL_SCANCODE_LSHIFT, 0, 0}, {SDL_SCANCODE_RSHIFT, 0, 0},
        {SDL_SCANCODE_Z, 0, 1}, {SDL_SCANCODE_X, 0, 2},
        {SDL_SCANCODE_C, 0, 3}, {SDL_SCANCODE_V, 0, 4},
        {SDL_SCANCODE_A, 1, 0}, {SDL_SCANCODE_S, 1, 1},
        {SDL_SCANCODE_D, 1, 2}, {SDL_SCANCODE_F, 1, 3},
        {SDL_SCANCODE_G, 1, 4},
        {SDL_SCANCODE_Q, 2, 0}, {SDL_SCANCODE_W, 2, 1},
        {SDL_SCANCODE_E, 2, 2}, {SDL_SCANCODE_R, 2, 3},
        {SDL_SCANCODE_T, 2, 4},
        {SDL_SCANCODE_1, 3, 0}, {SDL_SCANCODE_2, 3, 1},
        {SDL_SCANCODE_3, 3, 2}, {SDL_SCANCODE_4, 3, 3},
        {SDL_SCANCODE_5, 3, 4},
        {SDL_SCANCODE_KP_1, 3, 0}, {SDL_SCANCODE_KP_2, 3, 1},
        {SDL_SCANCODE_KP_3, 3, 2}, {SDL_SCANCODE_KP_4, 3, 3},
        {SDL_SCANCODE_KP_5, 3, 4},
        {SDL_SCANCODE_0, 4, 0}, {SDL_SCANCODE_9, 4, 1},
        {SDL_SCANCODE_8, 4, 2}, {SDL_SCANCODE_7, 4, 3},
        {SDL_SCANCODE_6, 4, 4},
        {SDL_SCANCODE_KP_0, 4, 0}, {SDL_SCANCODE_KP_9, 4, 1},
        {SDL_SCANCODE_KP_8, 4, 2}, {SDL_SCANCODE_KP_7, 4, 3},
        {SDL_SCANCODE_KP_6, 4, 4},
        {SDL_SCANCODE_P, 5, 0}, {SDL_SCANCODE_O, 5, 1},
        {SDL_SCANCODE_I, 5, 2}, {SDL_SCANCODE_U, 5, 3},
        {SDL_SCANCODE_Y, 5, 4},
        {SDL_SCANCODE_RETURN, 6, 0}, {SDL_SCANCODE_KP_ENTER, 6, 0},
        {SDL_SCANCODE_L, 6, 1}, {SDL_SCANCODE_K, 6, 2},
        {SDL_SCANCODE_J, 6, 3}, {SDL_SCANCODE_H, 6, 4},
        {SDL_SCANCODE_SPACE, 7, 0}, {SDL_SCANCODE_PERIOD, 7, 1},
        {SDL_SCANCODE_LCTRL, 7, 1}, {SDL_SCANCODE_RCTRL, 7, 1},
        {SDL_SCANCODE_M, 7, 2}, {SDL_SCANCODE_N, 7, 3},
        {SDL_SCANCODE_B, 7, 4},
    };
    for (const Map& m : kmap)
    {
        if (g_keymap == "wasd"
            && (m.sc == SDL_SCANCODE_W || m.sc == SDL_SCANCODE_A
                || m.sc == SDL_SCANCODE_S || m.sc == SDL_SCANCODE_D
                || m.sc == SDL_SCANCODE_LCTRL || m.sc == SDL_SCANCODE_RCTRL))
        {
            continue;
        }
        if (key_down(ks, m.sc))
        {
            ula.setKey(m.row, m.bit, true);
        }
    }

    uint8_t joy = 0;
    if (key_down(ks, SDL_SCANCODE_RIGHT))
    {
        joy |= 0x01;
    }
    if (key_down(ks, SDL_SCANCODE_LEFT))
    {
        joy |= 0x02;
    }
    if (key_down(ks, SDL_SCANCODE_DOWN))
    {
        joy |= 0x04;
    }
    if (key_down(ks, SDL_SCANCODE_UP))
    {
        joy |= 0x08;
    }
    if (key_down(ks, SDL_SCANCODE_LALT) || key_down(ks, SDL_SCANCODE_RALT))
    {
        joy |= 0x10;
    }
    if (g_keymap == "wasd")
    {
        if (key_down(ks, SDL_SCANCODE_W) || key_down(ks, SDL_SCANCODE_UP))
        {
            ula.setKey(1, 0, true); /* A = forward */
            joy |= 0x08;
        }
        if (key_down(ks, SDL_SCANCODE_S) || key_down(ks, SDL_SCANCODE_DOWN))
        {
            joy |= 0x04;
        }
        if (key_down(ks, SDL_SCANCODE_A) || key_down(ks, SDL_SCANCODE_LEFT))
        {
            ula.setKey(0, 1, true); /* Z = left */
            joy |= 0x02;
        }
        if (key_down(ks, SDL_SCANCODE_D) || key_down(ks, SDL_SCANCODE_RIGHT))
        {
            ula.setKey(0, 2, true); /* X = right */
            joy |= 0x01;
        }
        if (key_down(ks, SDL_SCANCODE_LCTRL) || key_down(ks, SDL_SCANCODE_RCTRL))
        {
            ula.setKey(2, 0, true); /* Q = jump */
            joy |= 0x10;
        }
    }
    ula.setKempston(static_cast<uint8_t>(joy | g_joy_pad | g_joy_axis | g_joy_hat));
}

static void pump_input()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                if (!event.key.repeat)
                {
                    log_debug("key down name=%s scancode=%s (%d) sdlk=%d",
                             SDL_GetKeyName(event.key.keysym.sym),
                             SDL_GetScancodeName(event.key.keysym.scancode),
                             static_cast<int>(event.key.keysym.scancode),
                             static_cast<int>(event.key.keysym.sym));
                    handle_key(event.key.keysym.sym, true);
                    latch_scancode(event.key.keysym.scancode);
                }
                break;
            case SDL_KEYUP:
                log_debug("key up   name=%s scancode=%s (%d)",
                         SDL_GetKeyName(event.key.keysym.sym),
                         SDL_GetScancodeName(event.key.keysym.scancode),
                         static_cast<int>(event.key.keysym.scancode));
                handle_key(event.key.keysym.sym, false);
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                {
                    log_info("window focus gained");
                }
                else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                {
                    log_info("window focus lost — click the ZXEm window, then press 1 then 0");
                }
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
            case SDL_JOYHATMOTION:
                handle_joy_hat(event.jhat.value);
                break;
            case SDL_JOYDEVICEADDED:
                if (joystick == nullptr)
                {
                    joystick = SDL_JoystickOpen(event.jdevice.which);
                    if (joystick)
                    {
                        log_info("opened joystick: %s", SDL_JoystickName(joystick));
                    }
                }
                break;
            case SDL_JOYDEVICEREMOVED:
                if (joystick != nullptr
                    && event.jdevice.which == SDL_JoystickInstanceID(joystick))
                {
                    log_info("joystick removed");
                    SDL_JoystickClose(joystick);
                    joystick = nullptr;
                    g_joy_pad = 0;
                    g_joy_axis = 0;
                    g_joy_hat = 0;
                }
                break;
            default:
                break;
        }
    }
    apply_spectrum_keys();
}

static void set_window_icon(SDL_Window* win, const char* argv0)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;
    paths.emplace_back("icons/zxem.png");
    if (argv0 != nullptr && argv0[0] != 0)
    {
        const fs::path dir = fs::path(argv0).parent_path();
        paths.push_back(dir / "icons" / "zxem.png");
        paths.push_back(dir / ".." / "icons" / "zxem.png");
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0)
    {
        log_debug("SDL_image PNG: %s", IMG_GetError());
        return;
    }
    for (const fs::path& p : paths)
    {
        SDL_Surface* surf = IMG_Load(p.string().c_str());
        if (surf == nullptr)
        {
            continue;
        }
        SDL_SetWindowIcon(win, surf);
        SDL_FreeSurface(surf);
        log_debug("window icon %s", p.string().c_str());
        return;
    }
}

static void print_usage(const char* argv0)
{
    fprintf(stderr,
            "Usage: %s [options] [input…]\n"
            "\n"
            "  input    Snapshot, tape, disk, poke, ROM, directory, or zip archive.\n"
            "           Options and inputs may be interleaved.\n"
            "           Directories expand to playable ZX files (non-recursive).\n"
            "           Zip archives are read in-place (no extract). Use zip#member\n"
            "           or --member NAME to select a file inside.\n"
            "           Precedence: CLI flags > --config INI > compiled defaults.\n"
            "\n"
            "Options:\n"
            "  -h, --help            Show this help and exit\n"
            "  -v, --version         Show version and exit\n"
            "      --list            List playable members to stdout\n"
            "      --member NAME     Load this zip member (substring match OK)\n"
            "      --model MODEL     spectrum48 (default), spectrum128, or plus3\n"
            "      --rom FILE        Load a 16K/32K/64K ROM image\n"
            "      --rom-dir DIR     Search DIR for a ROM (default: ./rom)\n"
            "      --no-system-rom   Do not search /usr/share/fuse (default: search)\n"
            "      --trdos-rom FILE  16K TR-DOS ROM (Beta Disk paging)\n"
            "      --plus3-rom FILE  64K +3 ROM, or a directory of plus3-0..3.rom\n"
            "      --config FILE     INI (default: ./config.ini); CLI flags always win\n"
            "      --pok FILE        Apply POK cheats after load\n"
            "      --headless        No SDL window/audio/input\n"
            "      --frames N        Run N frames then exit (implies --headless)\n"
            "      --no-audio        Disable audio (windowed; default: on)\n"
            "      --no-log          Disable RE logging (default: on, stderr)\n"
            "      --log-file PATH   Also write RE log to PATH\n"
            "      --log-level LVL   error|warn|info|debug|trace (default: info)\n"
            "      --trace-cpu       Log every instruction (startup; default: off)\n"
            "      --trace-io        Log I/O ports (default: off)\n"
            "      --keymap NAME     spectrum (default) or wasd (WASD+LCtrl)\n"
            "      --issue 2|3       48K ULA keyboard bits 5/7 (default: 3)\n"
            "      --disk-out FILE   Write dirty TRD/DSK image on exit (default: discard)\n"
            "      --verbose         Same as --log-level debug\n"
            "\n"
            "Formats: %s\n"
            "If no ROM is provided, a minimal synthetic ROM is generated.\n"
            "\n"
            "zxem %s\n",
            argv0, media_format_help(), ZXEM_VERSION);
}

static bool take_value(int& i, int argc, char** argv, const char* opt, std::string& out)
{
    const char* eq = strchr(argv[i], '=');
    if (eq != nullptr && eq[1] != '\0' && strncmp(argv[i], opt, static_cast<size_t>(eq - argv[i])) == 0)
    {
        out = eq + 1;
        return true;
    }
    if (i + 1 >= argc)
    {
        fprintf(stderr, "Missing argument for %s\n", opt);
        return false;
    }
    out = argv[++i];
    return true;
}

/**
 * @brief Copy INI keys onto process globals (compiled defaults already set).
 *
 * @param[in] cfg Loaded INI.
 * @note CLI is re-parsed after this so argv always wins over INI (AUDIT FE-3),
 *       regardless of whether --config appears before or after other flags.
 */
static void apply_config(const Config& cfg)
{
    if (cfg.has("emulator", "game"))
    {
        g_game_path = cfg.getString("emulator", "game", g_game_path);
    }
    if (cfg.has("emulator", "model"))
    {
        g_model = cfg.getString("emulator", "model", g_model);
    }
    if (cfg.has("rom", "file"))
    {
        g_rom_path = cfg.getString("rom", "file", g_rom_path);
    }
    if (cfg.has("rom", "dir"))
    {
        g_rom_dir = cfg.getString("rom", "dir", g_rom_dir);
    }
    if (cfg.has("input", "keymap"))
    {
        g_keymap = cfg.getString("input", "keymap", g_keymap);
    }
    if (cfg.has("hardware", "issue"))
    {
        g_issue = cfg.getString("hardware", "issue", g_issue);
    }
    if (cfg.has("video", "scale"))
    {
        g_scale = cfg.getInt("video", "scale", g_scale);
        if (g_scale < 1)
        {
            g_scale = 1;
        }
        if (g_scale > 6)
        {
            g_scale = 6;
        }
    }
}

static int list_playable(const std::string& spec)
{
    std::string archive;
    std::string member;
    const bool zip = vfs_split_spec(spec, archive, member);
    std::vector<std::string> names;
    if (zip || vfs_is_zip(spec))
    {
        names = vfs_find_members(archive.empty() ? spec : archive,
                                 g_member.empty() ? member : g_member);
    }
    else if (vfs_is_dir(spec))
    {
        names = vfs_expand_dir(spec);
    }
    else if (vfs_is_file(spec))
    {
        names = {spec};
    }
    else
    {
        fprintf(stderr, "Cannot list: %s\n", spec.c_str());
        return 1;
    }
    for (const auto& n : names)
    {
        printf("%s\n", n.c_str());
    }
    fprintf(stderr, "listed %zu playable image(s)\n", names.size());
    return 0;
}

static std::string resolve_input(const std::string& spec)
{
    std::string archive;
    std::string member;
    const bool zip = vfs_split_spec(spec, archive, member);
    if (!g_member.empty())
    {
        member = g_member;
    }
    if (zip || vfs_is_zip(spec))
    {
        const std::string zip_path = archive.empty() ? spec : archive;
        if (member.empty())
        {
            return {}; /* caller should list */
        }
        return zip_path + "#" + member;
    }
    if (vfs_is_dir(spec))
    {
        auto files = vfs_expand_dir(spec);
        if (!g_member.empty())
        {
            const std::string needle = vfs_lower(g_member);
            std::vector<std::string> hits;
            for (const auto& f : files)
            {
                if (vfs_lower(f).find(needle) != std::string::npos)
                {
                    hits.push_back(f);
                }
            }
            if (hits.size() == 1)
            {
                return hits[0];
            }
            if (hits.empty())
            {
                log_error("no match for --member %s in %s", g_member.c_str(), spec.c_str());
                return {};
            }
            log_error("ambiguous --member %s (%zu hits)", g_member.c_str(), hits.size());
            return {};
        }
        if (files.size() == 1)
        {
            return files[0];
        }
        return {};
    }
    return spec;
}

/** @brief True when ZXEM_HEARTBEAT is set to a non-empty value other than "0". */
static bool heartbeat_enabled(void)
{
    const char* e = std::getenv("ZXEM_HEARTBEAT");
    return e != nullptr && e[0] != '\0' && std::strcmp(e, "0") != 0;
}

/**
 * @brief FNV-1a of the 6912-byte display file (0x4000–0x5AFF).
 * @param u ULA whose @c ram[0..6911] is the current screen.
 * @return 32-bit hash (stable across runs for the same bytes).
 */
static uint32_t hash_display_file(const ULA& u)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6912; i++)
    {
        h ^= u.ram[i];
        h *= 16777619u;
    }
    return h;
}

/**
 * @brief Spectrum FRAMES system variable at 0x5C78 (24-bit little-endian).
 * @note ROM IM 1 ISR increments this; a stuck HALT with dead INT does not.
 */
static uint32_t read_frames_sysvar(const ULA& u)
{
    constexpr int kOff = 0x5C78 - 0x4000;
    return static_cast<uint32_t>(u.ram[kOff])
         | (static_cast<uint32_t>(u.ram[kOff + 1]) << 8)
         | (static_cast<uint32_t>(u.ram[kOff + 2]) << 16);
}

/**
 * @brief Emit a liveness sample (stdout always; info log if logging is on).
 *
 * Format is parsed by @c batch_test.py / @c tests/test_liveness.py:
 * @c Frame N, PC=0x.... SP=0x.... FRAMES=0x...... scr=xxxxxxxx
 */
static void print_heartbeat(int frame, const Z80& cpu, const ULA& u)
{
    const uint32_t frames = read_frames_sysvar(u);
    const uint32_t scr = hash_display_file(u);
    printf("Frame %d, PC=0x%04X SP=0x%04X FRAMES=0x%06X scr=%08x\n",
           frame, cpu.PC, cpu.SP, frames, scr);
    fflush(stdout);
    log_info("frame %d PC=0x%04X SP=0x%04X FRAMES=0x%06X scr=%08x",
             frame, cpu.PC, cpu.SP, frames, scr);
}

/**
 * @brief Parse argv into process-wide CLI globals.
 *
 * @param[in]  argc Argument count.
 * @param[in]  argv Argument vector.
 * @param[out] inputs       Positional paths (cleared then filled).
 * @param[out] want_help    Set if -h/--help is present.
 * @param[out] want_version Set if -v/--version is present.
 * @param[out] saw_config   Set if --config is present (path stored in g_config_path).
 * @return 0 on success, 1 on a usage or value error (message already printed).
 *
 * @note Invoked twice from main: pass 1 discovers --config so the INI can be
 *       applied; pass 2 reapplies CLI so flags always win over INI, independent
 *       of argv order (AUDIT FE-3). --config does not call apply_config here.
 */
static int parse_cli(int argc, char** argv,
                     std::vector<std::string>& inputs,
                     bool& want_help,
                     bool& want_version,
                     bool& saw_config)
{
    inputs.clear();
    want_help = false;
    want_version = false;
    saw_config = false;

    for (int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
        {
            want_help = true;
        }
        else if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0)
        {
            want_version = true;
        }
        else if (strcmp(a, "--list") == 0)
        {
            g_list_only = true;
        }
        else if (strcmp(a, "--headless") == 0)
        {
            g_headless = true;
        }
        else if (strcmp(a, "--no-audio") == 0)
        {
            g_no_audio = true;
        }
        else if (strcmp(a, "--no-log") == 0)
        {
            Log::instance().set_enabled(false);
        }
        else if (strcmp(a, "--verbose") == 0)
        {
            Log::instance().set_level(LogLevel::debug);
        }
        else if (strcmp(a, "--trace-cpu") == 0)
        {
            Log::instance().set_trace_cpu(true);
            Log::instance().set_level(LogLevel::trace);
        }
        else if (strcmp(a, "--trace-io") == 0)
        {
            Log::instance().set_trace_io(true);
        }
        else if (strncmp(a, "--frames", 8) == 0)
        {
            std::string v;
            if (!take_value(i, argc, argv, "--frames", v))
            {
                return 1;
            }
            g_max_frames = std::atoi(v.c_str());
            if (g_max_frames <= 0)
            {
                fprintf(stderr, "Error: --frames requires a positive integer\n");
                return 1;
            }
            g_headless = true;
        }
        else if (strncmp(a, "--config", 8) == 0)
        {
            if (!take_value(i, argc, argv, "--config", g_config_path))
            {
                return 1;
            }
            saw_config = true;
        }
        else if (strncmp(a, "--keymap", 8) == 0)
        {
            if (!take_value(i, argc, argv, "--keymap", g_keymap))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--issue", 7) == 0)
        {
            if (!take_value(i, argc, argv, "--issue", g_issue))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--disk-out", 10) == 0)
        {
            if (!take_value(i, argc, argv, "--disk-out", g_disk_out))
            {
                return 1;
            }
        }
        else if (strcmp(a, "--no-system-rom") == 0)
        {
            g_no_system_rom = true;
        }
        else if (strncmp(a, "--rom-dir", 9) == 0)
        {
            if (!take_value(i, argc, argv, "--rom-dir", g_rom_dir))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--trdos-rom", 11) == 0)
        {
            if (!take_value(i, argc, argv, "--trdos-rom", g_trdos_rom_path))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--plus3-rom", 11) == 0)
        {
            if (!take_value(i, argc, argv, "--plus3-rom", g_plus3_rom_path))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--rom", 5) == 0)
        {
            if (!take_value(i, argc, argv, "--rom", g_rom_path))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--model", 7) == 0)
        {
            if (!take_value(i, argc, argv, "--model", g_model))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--member", 8) == 0)
        {
            if (!take_value(i, argc, argv, "--member", g_member))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--pok", 5) == 0)
        {
            if (!take_value(i, argc, argv, "--pok", g_pok_path))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--log-file", 10) == 0)
        {
            if (!take_value(i, argc, argv, "--log-file", g_log_file))
            {
                return 1;
            }
        }
        else if (strncmp(a, "--log-level", 11) == 0)
        {
            std::string v;
            if (!take_value(i, argc, argv, "--log-level", v))
            {
                return 1;
            }
            bool ok = false;
            const LogLevel lv = Log::parse_level(v.c_str(), &ok);
            if (!ok)
            {
                fprintf(stderr, "Unknown --log-level %s\n", v.c_str());
                return 1;
            }
            Log::instance().set_level(lv);
        }
        else if (a[0] == '-')
        {
            fprintf(stderr, "Unknown option: %s\n", a);
            print_usage(argv[0]);
            return 1;
        }
        else
        {
            inputs.push_back(a);
        }
    }
    return 0;
}

int main(int argc, char* argv[])
{
    std::vector<std::string> inputs;
    bool want_help = false;
    bool want_version = false;
    bool saw_config_flag = false;

    /*
     * Precedence: compiled defaults < INI < CLI.
     * Pass 1 reads --config (anywhere on argv) so the INI path is known.
     * apply_config then fills globals from INI.
     * Pass 2 reapplies CLI so flags win even when they appear before --config.
     */
    if (parse_cli(argc, argv, inputs, want_help, want_version, saw_config_flag) != 0)
    {
        return 1;
    }

    Config cfg;
    if (cfg.load(g_config_path.c_str()))
    {
        apply_config(cfg);
    }
    else if (saw_config_flag)
    {
        fprintf(stderr, "Warning: could not load config %s\n", g_config_path.c_str());
    }

    if (parse_cli(argc, argv, inputs, want_help, want_version, saw_config_flag) != 0)
    {
        return 1;
    }

    if (want_help || argc == 1)
    {
        print_usage(argv[0]);
        return 0;
    }
    if (want_version)
    {
        printf("zxem %s\n", ZXEM_VERSION);
        return 0;
    }

    if (!g_log_file.empty())
    {
        if (!Log::instance().set_file(g_log_file.c_str()))
        {
            fprintf(stderr, "Cannot open --log-file %s\n", g_log_file.c_str());
            return 1;
        }
    }

    if (g_list_only)
    {
        if (inputs.empty())
        {
            fprintf(stderr, "--list requires an archive or directory\n");
            return 1;
        }
        int rc = 0;
        for (const auto& in : inputs)
        {
            rc |= list_playable(in);
        }
        return rc;
    }

    if (inputs.size() > 1)
    {
        fprintf(stderr, "Multiple inputs given; using the first (%s). Use --list for catalogs.\n",
                inputs[0].c_str());
    }

    if (!inputs.empty())
    {
        const std::string resolved = resolve_input(inputs[0]);
        if (resolved.empty())
        {
            /* Directory/zip catalog: list to stdout (product) instead of failing. */
            return list_playable(inputs[0]);
        }
        g_game_path = resolved;
    }

    if (g_game_path.empty())
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string m = g_model;
    for (char& c : m)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (m == "spectrum48" || m == "48" || m == "spec48")
    {
        g_model = "spectrum48";
        ula.setModel128(false);
    }
    else if (m == "spectrum128" || m == "128" || m == "spec128" || m == "128k")
    {
        g_model = "spectrum128";
        ula.setModel128(true);
    }
    else if (m == "plus3" || m == "spectrum+3" || m == "+3" || m == "spectrumplus3")
    {
        g_model = "plus3";
        ula.setPlus3(true);
    }
    else
    {
        fprintf(stderr, "Error: unsupported model '%s'. Supported: spectrum48, spectrum128, plus3\n",
                g_model.c_str());
        return 1;
    }

    for (char& c : g_keymap)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (g_keymap != "spectrum" && g_keymap != "wasd")
    {
        fprintf(stderr, "Error: unsupported keymap '%s'. Supported: spectrum, wasd\n",
                g_keymap.c_str());
        return 1;
    }
    log_info("keymap=%s", g_keymap.c_str());

    for (char& c : g_issue)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (g_issue == "issue2")
    {
        g_issue = "2";
    }
    else if (g_issue == "issue3")
    {
        g_issue = "3";
    }
    if (g_issue != "2" && g_issue != "3")
    {
        fprintf(stderr, "Error: unsupported --issue '%s'. Supported: 2, 3\n",
                g_issue.c_str());
        return 1;
    }
    if (g_issue == "2" && ula.is128)
    {
        log_warn("issue 2 floating bits apply to 48K only; 128K/+3 stay issue 3");
    }
    ula.set_issue2(g_issue == "2");
    log_info("ula issue=%s", g_issue.c_str());

    if (g_headless)
    {
        if (SDL_Init(0) < 0)
        {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
    }
    else
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0)
        {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }

        window = SDL_CreateWindow("ZXEm " ZXEM_VERSION,
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  ULA::SCREEN_WIDTH * g_scale, ULA::SCREEN_HEIGHT * g_scale,
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
        if (!window)
        {
            fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        set_window_icon(window, argv[0]);
        /* Env SDL_RENDER_VSYNC=1 / SCALE_QUALITY=best win over SetHint unless OVERRIDE. */
        SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, "0", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "0", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority("SDL_RENDER_DRIVER", "opengl", SDL_HINT_DEFAULT);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer)
        {
            fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        SDL_RendererInfo rinfo{};
        if (SDL_GetRendererInfo(renderer, &rinfo) == 0)
        {
            log_info("SDL renderer=%s flags=0x%x vsync_hint=0 video=%s",
                     rinfo.name ? rinfo.name : "?", rinfo.flags,
                     SDL_GetCurrentVideoDriver());
            if (rinfo.flags & SDL_RENDERER_SOFTWARE)
            {
                log_warn("SDL is using the software renderer");
            }
        }
        /* Hint may not reach the GL context; 60 Hz vsync on a 50 Hz emu feels sluggish. */
        if (SDL_GL_SetSwapInterval(0) != 0)
        {
            log_debug("SDL_GL_SetSwapInterval(0): %s", SDL_GetError());
        }
        std::signal(SIGINT, zxem_on_signal);
        std::signal(SIGTERM, zxem_on_signal);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    ULA::SCREEN_WIDTH, ULA::SCREEN_HEIGHT);
        if (!texture)
        {
            fprintf(stderr, "Texture creation failed: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        if (!g_no_audio)
        {
            init_audio();
        }
        SDL_RaiseWindow(window);
        SDL_SetWindowInputFocus(window);

        if (SDL_NumJoysticks() > 0)
        {
            joystick = SDL_JoystickOpen(0);
            if (joystick)
            {
                log_info("opened joystick: %s", SDL_JoystickName(joystick));
            }
        }
    }

    z80.ula = &ula;

    bool rom_loaded = false;
    if (!g_rom_path.empty())
    {
        rom_loaded = load_rom_file(g_rom_path.c_str(), ula);
        if (!rom_loaded)
        {
            log_warn("failed to load ROM %s", g_rom_path.c_str());
        }
    }
    if (!rom_loaded && !g_plus3_rom_path.empty())
    {
        rom_loaded = load_rom_file(g_plus3_rom_path.c_str(), ula);
        if (!rom_loaded)
        {
            rom_loaded = load_plus3_roms_from_dir(g_plus3_rom_path.c_str(), ula);
        }
        if (!rom_loaded)
        {
            log_warn("failed to load +3 ROM %s", g_plus3_rom_path.c_str());
        }
    }
    if (!rom_loaded && g_model == "plus3")
    {
        rom_loaded = load_plus3_roms_from_dir(g_rom_dir.c_str(), ula);
    }
    if (!rom_loaded)
    {
        rom_loaded = load_rom_from_dir(g_rom_dir.c_str(), ula);
    }
    if (!rom_loaded && !g_no_system_rom)
    {
        rom_loaded = load_system_roms(ula, g_model.c_str());
        if (rom_loaded)
        {
            log_info("loaded system ROM for model %s", g_model.c_str());
        }
    }
    if (!rom_loaded)
    {
        ula.resetToSyntheticROM();
        log_info("no ROM found: using synthetic ROM");
    }
    if (!g_trdos_rom_path.empty())
    {
        if (!load_trdos_rom_file(g_trdos_rom_path.c_str(), ula))
        {
            log_warn("failed to load TR-DOS ROM %s", g_trdos_rom_path.c_str());
        }
    }
    else
    {
        char trp[512];
        snprintf(trp, sizeof(trp), "%s/trdos.rom", g_rom_dir.c_str());
        struct stat st{};
        if (stat(trp, &st) == 0 && S_ISREG(st.st_mode))
        {
            load_trdos_rom_file(trp, ula);
        }
    }

    if (!load_game_spec(g_game_path))
    {
        fprintf(stderr, "Failed to load %s\n", g_game_path.c_str());
        SDL_Quit();
        return 1;
    }

    log_info("ready PC=0x%04X SP=0x%04X model=%s 128=%d",
             z80.PC, z80.SP, g_model.c_str(), ula.is128 ? 1 : 0);
    printf("Loaded %s PC=0x%04X SP=0x%04X model=%s\n",
           g_game_path.c_str(), z80.PC, z80.SP, g_model.c_str());
    g_heartbeat = heartbeat_enabled();
    if (g_heartbeat)
    {
        print_heartbeat(0, z80, ula);
    }
    if (!g_headless)
    {
        fprintf(stderr, "Click the ZXEm window, then press keys. Each press is logged as 'key down'.\n");
    }

#if defined(__SANITIZE_ADDRESS__)
    if (!g_headless)
    {
        fprintf(stderr,
                "zxem: debug+ASan build is slow. For play: make -s release && ./zxem GAME\n");
    }
#endif

    int (Z80::*cpu_step)() = &Z80::execute;
    if (Log::instance().trace_cpu())
    {
        cpu_step = &Z80::execute_traced;
    }

    const uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t next_deadline = SDL_GetPerformanceCounter();
    int frame_count = 0;

    while (running)
    {
        if (g_exit_req)
        {
            running = false;
            break;
        }
        if (!g_headless)
        {
            pump_input();
        }

        const int frame_t = ula.t_frame();
        const int poll_div = frame_t / 4;
        int tstates_this_frame = 0;
        int poll_acc = 0;
        while (tstates_this_frame < frame_t)
        {
            const int ts = (z80.*cpu_step)();
            ula.step(ts);
            if (!g_headless && !g_no_audio)
            {
                updateAudio(ts);
            }
            tstates_this_frame += ts;
            poll_acc += ts;
            if (!g_headless && poll_acc >= poll_div)
            {
                poll_acc = 0;
                pump_input();
            }

            if (ula.take_frame_irq())
            {
                if (z80.can_take_irq())
                {
                    const int irq_t = z80.irq_ack();
                    ula.step(irq_t);
                    if (!g_headless && !g_no_audio)
                    {
                        updateAudio(irq_t);
                    }
                    tstates_this_frame += irq_t;
                    poll_acc += irq_t;
                }
            }
        }

        if (!g_headless)
        {
            uint32_t pixels[ULA::SCREEN_WIDTH * ULA::SCREEN_HEIGHT];
            ula.renderFrame(pixels, ULA::SCREEN_WIDTH * 4);
            SDL_UpdateTexture(texture, nullptr, pixels, ULA::SCREEN_WIDTH * 4);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }

        frame_count++;
        if (frame_count % 50 == 0)
        {
            log_debug("frame %d PC=0x%04X", frame_count, z80.PC);
        }

        const bool last_frame = (g_max_frames > 0 && frame_count >= g_max_frames);
        if (g_heartbeat && (frame_count == 1 || frame_count % 50 == 0 || last_frame))
        {
            print_heartbeat(frame_count, z80, ula);
        }

        if (last_frame)
        {
            printf("Reached target frame count %d, exiting.\n", g_max_frames);
            running = false;
        }

        if (!g_headless)
        {
            const uint64_t frame_ticks =
                (perf_freq * static_cast<uint64_t>(ula.t_frame())) /
                static_cast<uint64_t>(ula.cpu_hz());
            next_deadline += frame_ticks;
            const uint64_t now = SDL_GetPerformanceCounter();
            if (now < next_deadline)
            {
                const uint64_t remain_ticks = next_deadline - now;
                const uint64_t remain_us = remain_ticks * 1000000ull / perf_freq;
                const uint32_t delay_ms = static_cast<uint32_t>(remain_us / 1000ull);
                if (delay_ms > 0)
                {
                    SDL_Delay(delay_ms);
                }
                while (SDL_GetPerformanceCounter() < next_deadline)
                {
                }
            }
            else if (now - next_deadline > frame_ticks)
            {
                /* Missed more than one frame (stall): re-anchor, do not turbo. */
                next_deadline = now;
            }
            end_ula_frame_latches();
        }
    }

    if (disk_dirty(ula))
    {
        if (g_disk_out.empty())
        {
            log_warn("disk changes discarded (pass --disk-out FILE to keep them)");
        }
        else if (!disk_save(ula, g_disk_out.c_str()))
        {
            log_error("disk-out failed");
        }
    }

    if (joystick)
    {
        SDL_JoystickClose(joystick);
    }
    if (audio_dev)
    {
        SDL_CloseAudioDevice(audio_dev);
    }
    if (texture)
    {
        SDL_DestroyTexture(texture);
    }
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
    }
    if (window)
    {
        SDL_DestroyWindow(window);
    }
    IMG_Quit();
    SDL_Quit();
    return 0;
}
