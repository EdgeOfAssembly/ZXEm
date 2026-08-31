/**
 * @file hz50test.cpp
 * @brief Tiny CLI: xrandr to ~50 Hz, wait, restore previous mode.
 *
 * Uses the xrandr(1) program (NVIDIA-friendly). Does not open a GL window.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>

static const char* kVersion = "0.1";

static std::string g_output;
static std::string g_saved_mode;
static std::string g_mode50;
static bool g_changed = false;

static void print_usage(const char* argv0)
{
    std::fprintf(stderr,
                 "Usage: %s [options]\n"
                 "\n"
                 "  Switch the current X11 output to a ~50 Hz mode, wait, restore.\n"
                 "  Uses xrandr(1). No GL window (safer than ZXEm's RandR path).\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help         Show this help and exit\n"
                 "  -v, --version      Show version and exit\n"
                 "      --seconds N    Hold 50 Hz for N seconds (default: 5)\n"
                 "      --dry-run      Print planned xrandr lines; do not change mode\n"
                 "\n"
                 "%s 0.1\n",
                 argv0, argv0);
}

static int run_xrandr(const char* arg1, const char* arg2, const char* arg3, const char* arg4)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        std::perror("fork");
        return 1;
    }
    if (pid == 0)
    {
        if (arg4 != nullptr)
        {
            execlp("xrandr", "xrandr", arg1, arg2, arg3, arg4, static_cast<char*>(nullptr));
        }
        else
        {
            execlp("xrandr", "xrandr", arg1, static_cast<char*>(nullptr));
        }
        std::perror("xrandr");
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0)
    {
        return 1;
    }
    if (WIFEXITED(st))
    {
        return WEXITSTATUS(st);
    }
    return 1;
}

static void restore()
{
    if (!g_changed || g_output.empty() || g_saved_mode.empty())
    {
        return;
    }
    std::fprintf(stderr, "hz50test: restore %s --mode %s\n", g_output.c_str(),
                 g_saved_mode.c_str());
    (void)run_xrandr("--output", g_output.c_str(), "--mode", g_saved_mode.c_str());
    g_changed = false;
}

extern "C" {
static void on_sig(int)
{
    restore();
    _exit(128);
}
}

static bool parse_query()
{
    FILE* fp = popen("xrandr --query", "r");
    if (fp == nullptr)
    {
        std::perror("popen xrandr");
        return false;
    }
    char line[512];
    std::string cur_out;
    while (std::fgets(line, sizeof(line), fp) != nullptr)
    {
        if (std::strstr(line, " connected") != nullptr && std::strstr(line, "disconnected") == nullptr)
        {
            char name[128];
            if (std::sscanf(line, "%127s", name) == 1)
            {
                cur_out = name;
                if (g_output.empty())
                {
                    g_output = cur_out;
                }
            }
            continue;
        }
        if (cur_out.empty() || cur_out != g_output)
        {
            continue;
        }
        char mname[128];
        double rate = 0.0;
        if (std::sscanf(line, " %127s %lf", mname, &rate) != 2)
        {
            continue;
        }
        const bool current = std::strchr(line, '*') != nullptr;
        if (current)
        {
            g_saved_mode = mname;
        }
        if (std::strstr(mname, "_50") != nullptr || (rate > 49.0 && rate < 51.0))
        {
            g_mode50 = mname;
        }
    }
    (void)pclose(fp);
    return !g_output.empty() && !g_saved_mode.empty() && !g_mode50.empty();
}

int main(int argc, char** argv)
{
    int seconds = 5;
    bool dry = false;
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 0;
    }
    for (int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        if (std::strcmp(a, "-v") == 0 || std::strcmp(a, "--version") == 0)
        {
            std::printf("hz50test %s\n", kVersion);
            return 0;
        }
        if (std::strcmp(a, "--dry-run") == 0)
        {
            dry = true;
            continue;
        }
        if (std::strncmp(a, "--seconds", 9) == 0)
        {
            const char* val = nullptr;
            if (a[9] == '=' && a[10] != 0)
            {
                val = a + 10;
            }
            else if (i + 1 < argc)
            {
                val = argv[++i];
            }
            if (val == nullptr || std::sscanf(val, "%d", &seconds) != 1 || seconds < 1)
            {
                std::fprintf(stderr, "Error: --seconds needs a positive integer\n");
                return 1;
            }
            continue;
        }
        std::fprintf(stderr, "Unknown option: %s\n", a);
        print_usage(argv[0]);
        return 1;
    }

    if (!parse_query())
    {
        std::fprintf(stderr,
                     "Error: could not parse xrandr (need a connected output with a ~50 Hz mode)\n");
        return 1;
    }
    std::fprintf(stderr, "hz50test: output=%s current=%s pal50=%s hold=%ds\n",
                 g_output.c_str(), g_saved_mode.c_str(), g_mode50.c_str(), seconds);
    if (g_saved_mode == g_mode50)
    {
        std::fprintf(stderr, "hz50test: already on 50 Hz mode, nothing to do\n");
        return 0;
    }
    if (dry)
    {
        std::fprintf(stderr, "dry-run: xrandr --output %s --mode %s\n", g_output.c_str(),
                     g_mode50.c_str());
        std::fprintf(stderr, "dry-run: sleep %d\n", seconds);
        std::fprintf(stderr, "dry-run: xrandr --output %s --mode %s\n", g_output.c_str(),
                     g_saved_mode.c_str());
        return 0;
    }

    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);
    std::fprintf(stderr, "hz50test: xrandr --output %s --mode %s\n", g_output.c_str(),
                 g_mode50.c_str());
    if (run_xrandr("--output", g_output.c_str(), "--mode", g_mode50.c_str()) != 0)
    {
        std::fprintf(stderr, "Error: xrandr failed to set %s\n", g_mode50.c_str());
        return 1;
    }
    g_changed = true;
    std::fprintf(stderr, "hz50test: holding %d seconds (Ctrl-C restores)\n", seconds);
    for (int i = 0; i < seconds; i++)
    {
        sleep(1);
    }
    restore();
    return 0;
}
