/**
 * @file main.cpp
 * @brief ZXEm SDL2 front-end and command-line interface.
 */

#include <SDL2/SDL.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
static const int SAMPLE_RATE = 44100;
static int g_scale = 3;

static std::string g_game_path;
static std::string g_rom_dir = "rom";
static std::string g_rom_path;
static std::string g_trdos_rom_path;
static std::string g_plus3_rom_path;
static std::string g_config_path = "config.ini";
static std::string g_model = "spectrum48";
static bool g_no_system_rom = false;
static std::string g_member;
static std::string g_pok_path;
static std::string g_log_file;

static bool g_headless = false;
static bool g_no_audio = false;
static bool g_list_only = false;
static int g_max_frames = -1;

static const int AUDIO_BUFFER_SIZE = 16384;
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];
static volatile int audio_write_pos = 0;
static volatile int audio_read_pos = 0;

static void audio_callback(void*, uint8_t* stream, int len)
{
    int16_t* buf = reinterpret_cast<int16_t*>(stream);
    const int samples = len / 2;
    for (int i = 0; i < samples; i++)
    {
        const int rp = audio_read_pos;
        if (rp != audio_write_pos)
        {
            buf[i] = audio_buffer[rp];
            audio_read_pos = (rp + 1) % AUDIO_BUFFER_SIZE;
        }
        else
        {
            const float s = ula.currentAudioSample();
            buf[i] = static_cast<int16_t>(s * 12000.0f);
        }
    }
}

static void pushAudioSample(int16_t sample)
{
    const int wp = audio_write_pos;
    const int next = (wp + 1) % AUDIO_BUFFER_SIZE;
    if (next == audio_read_pos)
    {
        audio_read_pos = (audio_read_pos + 1) % AUDIO_BUFFER_SIZE;
    }
    audio_buffer[wp] = sample;
    audio_write_pos = next;
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

/** @brief 3.5 MHz / 44100 Hz ≈ 79 T-states per host sample. */
static const int T_PER_SAMPLE = 79;
static int audio_t_accum = 0;

static void updateAudio(int tstates)
{
    if (tstates <= 0)
    {
        return;
    }
    audio_t_accum += tstates;
    while (audio_t_accum >= T_PER_SAMPLE)
    {
        audio_t_accum -= T_PER_SAMPLE;
        ula.beeper_changed = false;
        const float s = ula.currentAudioSample();
        pushAudioSample(static_cast<int16_t>(s * 12000.0f));
    }
}

static void handle_joy_button(int button, bool pressed)
{
    switch (button)
    {
        case 0: ula.setKey(7, 0, pressed); break;
        case 1: ula.setKey(6, 0, pressed); break;
        case 6: ula.setKey(7, 0, pressed); break;
        case 7: ula.setKey(6, 0, pressed); break;
        default: break;
    }
}

static void handle_joy_axis(int axis, int16_t value)
{
    const int DEADZONE = 4096;
    if (axis == 0)
    {
        const bool left = value < -DEADZONE;
        const bool right = value > DEADZONE;
        uint8_t k = static_cast<uint8_t>(ula.kempston & ~0x03);
        if (right)
        {
            k = static_cast<uint8_t>(k | 0x01);
        }
        if (left)
        {
            k = static_cast<uint8_t>(k | 0x02);
        }
        ula.setKempston(k);
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

static uint8_t g_key_sticky[8][5];

static void sticky_press(int row, int bit)
{
    if (row < 0 || row > 7 || bit < 0 || bit > 4)
    {
        return;
    }
    g_key_sticky[row][bit] = 12; /* ~240 ms at 50 Hz — menu polls once per draw */
    ula.setKey(row, bit, true);
}

static void sticky_decay()
{
    for (int row = 0; row < 8; row++)
    {
        for (int bit = 0; bit < 5; bit++)
        {
            if (g_key_sticky[row][bit] == 0)
            {
                continue;
            }
            ula.setKey(row, bit, true);
            g_key_sticky[row][bit]--;
        }
    }
}

static void matrix_key(int row, int bit, bool pressed)
{
    if (pressed)
    {
        sticky_press(row, bit);
        return;
    }
    /* Same-poll KEYUP must not clear a tap; sticky_decay holds it. */
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
        case SDLK_F9:
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
        case SDLK_RSHIFT: matrix_key(0, 0, pressed); break;
        case SDLK_z: matrix_key(0, 1, pressed); break;
        case SDLK_x: matrix_key(0, 2, pressed); break;
        case SDLK_c: matrix_key(0, 3, pressed); break;
        case SDLK_v: matrix_key(0, 4, pressed); break;

        case SDLK_a: matrix_key(1, 0, pressed); break;
        case SDLK_s: matrix_key(1, 1, pressed); break;
        case SDLK_d: matrix_key(1, 2, pressed); break;
        case SDLK_f: matrix_key(1, 3, pressed); break;
        case SDLK_g: matrix_key(1, 4, pressed); break;

        case SDLK_q: matrix_key(2, 0, pressed); break;
        case SDLK_w: matrix_key(2, 1, pressed); break;
        case SDLK_e: matrix_key(2, 2, pressed); break;
        case SDLK_r: matrix_key(2, 3, pressed); break;
        case SDLK_t: matrix_key(2, 4, pressed); break;

        case SDLK_1:
        case SDLK_KP_1: matrix_key(3, 0, pressed); break;
        case SDLK_2:
        case SDLK_KP_2: matrix_key(3, 1, pressed); break;
        case SDLK_3:
        case SDLK_KP_3: matrix_key(3, 2, pressed); break;
        case SDLK_4:
        case SDLK_KP_4: matrix_key(3, 3, pressed); break;
        case SDLK_5:
        case SDLK_KP_5: matrix_key(3, 4, pressed); break;

        case SDLK_0:
        case SDLK_KP_0: matrix_key(4, 0, pressed); break;
        case SDLK_9:
        case SDLK_KP_9: matrix_key(4, 1, pressed); break;
        case SDLK_8:
        case SDLK_KP_8: matrix_key(4, 2, pressed); break;
        case SDLK_7:
        case SDLK_KP_7: matrix_key(4, 3, pressed); break;
        case SDLK_6:
        case SDLK_KP_6: matrix_key(4, 4, pressed); break;

        case SDLK_p: matrix_key(5, 0, pressed); break;
        case SDLK_o: matrix_key(5, 1, pressed); break;
        case SDLK_i: matrix_key(5, 2, pressed); break;
        case SDLK_u: matrix_key(5, 3, pressed); break;
        case SDLK_y: matrix_key(5, 4, pressed); break;

        case SDLK_RETURN: matrix_key(6, 0, pressed); break;
        case SDLK_l: matrix_key(6, 1, pressed); break;
        case SDLK_k: matrix_key(6, 2, pressed); break;
        case SDLK_j: matrix_key(6, 3, pressed); break;
        case SDLK_h: matrix_key(6, 4, pressed); break;

        case SDLK_SPACE: matrix_key(7, 0, pressed); break;
        case SDLK_PERIOD: matrix_key(7, 1, pressed); break;
        case SDLK_m: matrix_key(7, 2, pressed); break;
        case SDLK_n: matrix_key(7, 3, pressed); break;
        case SDLK_b: matrix_key(7, 4, pressed); break;

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
        if (ks[m.sc])
        {
            ula.setKey(m.row, m.bit, true);
        }
    }

    uint8_t joy = ula.kempston;
    if (ks[SDL_SCANCODE_RIGHT])
    {
        joy |= 0x01;
    }
    if (ks[SDL_SCANCODE_LEFT])
    {
        joy |= 0x02;
    }
    if (ks[SDL_SCANCODE_DOWN])
    {
        joy |= 0x04;
    }
    if (ks[SDL_SCANCODE_UP])
    {
        joy |= 0x08;
    }
    if (ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT])
    {
        joy |= 0x10;
    }
    ula.setKempston(joy);
    sticky_decay(); /* re-asserts still-sticky taps after memset */
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
                    log_info("key down name=%s scancode=%s (%d) sdlk=%d",
                             SDL_GetKeyName(event.key.keysym.sym),
                             SDL_GetScancodeName(event.key.keysym.scancode),
                             static_cast<int>(event.key.keysym.scancode),
                             static_cast<int>(event.key.keysym.sym));
                    handle_key(event.key.keysym.sym, true);
                }
                break;
            case SDL_KEYUP:
                log_info("key up   name=%s scancode=%s (%d)",
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
            default:
                break;
        }
    }
    apply_spectrum_keys();
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
            "      --config FILE     INI config (default: ./config.ini)\n"
            "      --pok FILE        Apply POK cheats after load\n"
            "      --headless        No SDL window/audio/input\n"
            "      --frames N        Run N frames then exit (implies --headless)\n"
            "      --no-audio        Disable audio (windowed; default: on)\n"
            "      --no-log          Disable RE logging (default: on, stderr)\n"
            "      --log-file PATH   Also write RE log to PATH\n"
            "      --log-level LVL   error|warn|info|debug|trace (default: info)\n"
            "      --trace-cpu       Log every instruction (startup; default: off)\n"
            "      --trace-io        Log I/O ports (default: off)\n"
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

int main(int argc, char* argv[])
{
    std::vector<std::string> inputs;
    bool want_help = false;
    bool want_version = false;
    bool saw_config_flag = false;

    Config cfg;
    cfg.load(g_config_path.c_str());

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
            if (!cfg.load(g_config_path.c_str()))
            {
                fprintf(stderr, "Warning: could not load config %s\n", g_config_path.c_str());
            }
            else
            {
                apply_config(cfg);
                saw_config_flag = true;
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

    /* Default config (no --config) still supplies model/rom if present. */
    if (!saw_config_flag)
    {
        apply_config(cfg);
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
        /* Env SDL_RENDER_VSYNC=1 wins over SetHint; a failed swap can block ~1 s (1 fps). */
        SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, "0", SDL_HINT_OVERRIDE);
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

    const int TSTATES_PER_FRAME = 69888;
    const int TARGET_FRAME_US = 20000;
    uint64_t last_frame_time = SDL_GetPerformanceCounter();
    const uint64_t perf_freq = SDL_GetPerformanceFrequency();
    int frame_count = 0;

    while (running)
    {
        if (!g_headless)
        {
            pump_input();
        }

        int tstates_this_frame = 0;
        int poll_acc = 0;
        while (tstates_this_frame < TSTATES_PER_FRAME)
        {
            const int ts = (z80.*cpu_step)();
            ula.step(ts);
            if (!g_headless && !g_no_audio)
            {
                updateAudio(ts);
            }
            tstates_this_frame += ts;
            poll_acc += ts;
            if (!g_headless && poll_acc >= 17472)
            {
                poll_acc = 0;
                pump_input();
            }

            if (ula.frame_tstates >= TSTATES_PER_FRAME)
            {
                ula.frame_tstates -= TSTATES_PER_FRAME;
                if (z80.IFF1)
                {
                    z80.IFF1 = z80.IFF2 = false;
                    z80.halted = false;
                    ula.step(7);
                    z80.SP = static_cast<uint16_t>(z80.SP - 2);
                    ula.write(z80.SP, static_cast<uint8_t>(z80.PC & 0xFF));
                    ula.write(static_cast<uint16_t>(z80.SP + 1), static_cast<uint8_t>(z80.PC >> 8));
                    if (z80.IM == 2)
                    {
                        const uint16_t vec = static_cast<uint16_t>((static_cast<uint16_t>(z80.I) << 8) | 0xFF);
                        const uint8_t lo = ula.read(vec);
                        const uint8_t hi = ula.read(static_cast<uint16_t>(vec + 1));
                        z80.PC = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
                    }
                    else
                    {
                        z80.PC = 0x0038;
                    }
                    tstates_this_frame += 7;
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
            log_info("frame %d PC=0x%04X (~50 frames; if this is ~1s wall, fps is ok)",
                     frame_count, z80.PC);
        }

        if (g_max_frames > 0 && frame_count >= g_max_frames)
        {
            printf("Reached target frame count %d, exiting.\n", g_max_frames);
            running = false;
        }

        if (!g_headless)
        {
            const uint64_t now = SDL_GetPerformanceCounter();
            const uint64_t elapsed_us = (now - last_frame_time) * 1000000 / perf_freq;
            if (elapsed_us < TARGET_FRAME_US)
            {
                SDL_Delay(static_cast<Uint32>((TARGET_FRAME_US - elapsed_us) / 1000));
            }
            last_frame_time = SDL_GetPerformanceCounter();
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
    SDL_Quit();
    return 0;
}
