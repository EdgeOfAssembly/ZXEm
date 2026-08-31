/**
 * @file hzlock.cpp
 * @brief libXrandr PAL 50 Hz lock for the current X11 screen.
 */

#include "hzlock.h"

#include "log.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <cmath>
#include <cstdlib>

namespace
{

Display* g_dpy = nullptr;
XRRScreenResources* g_res = nullptr;
RRCrtc g_crtc = None;
RRMode g_saved_mode = None;
RRMode g_new_mode = None;
int g_saved_x = 0;
int g_saved_y = 0;
Rotation g_saved_rot = RR_Rotate_0;
RROutput* g_saved_outputs = nullptr;
int g_saved_noutput = 0;
bool g_changed = false;

double mode_hz(const XRRModeInfo& m)
{
    if (m.hTotal == 0 || m.vTotal == 0)
    {
        return 0.0;
    }
    return (static_cast<double>(m.dotClock) * 1000.0)
        / (static_cast<double>(m.hTotal) * static_cast<double>(m.vTotal));
}

const XRRModeInfo* find_mode(XRRScreenResources* res, RRMode id)
{
    for (int i = 0; i < res->nmode; i++)
    {
        if (res->modes[i].id == id)
        {
            return &res->modes[i];
        }
    }
    return nullptr;
}

} // namespace

bool hz_lock_pal50()
{
    hz_lock_restore();
    g_dpy = XOpenDisplay(nullptr);
    if (g_dpy == nullptr)
    {
        log_info("hz-lock: no X display");
        return false;
    }
    int event_base = 0;
    int error_base = 0;
    if (XRRQueryExtension(g_dpy, &event_base, &error_base) == False)
    {
        log_warn("hz-lock: RandR not present");
        XCloseDisplay(g_dpy);
        g_dpy = nullptr;
        return false;
    }
    const int screen = DefaultScreen(g_dpy);
    const Window root = RootWindow(g_dpy, screen);
    g_res = XRRGetScreenResourcesCurrent(g_dpy, root);
    if (g_res == nullptr)
    {
        XCloseDisplay(g_dpy);
        g_dpy = nullptr;
        return false;
    }

    for (int c = 0; c < g_res->ncrtc; c++)
    {
        XRRCrtcInfo* ci = XRRGetCrtcInfo(g_dpy, g_res, g_res->crtcs[c]);
        if (ci == nullptr || ci->mode == None || ci->noutput == 0)
        {
            if (ci != nullptr)
            {
                XRRFreeCrtcInfo(ci);
            }
            continue;
        }
        const XRRModeInfo* cur = find_mode(g_res, ci->mode);
        if (cur == nullptr)
        {
            XRRFreeCrtcInfo(ci);
            continue;
        }
        const double cur_hz = mode_hz(*cur);
        RRMode best_id = None;
        double best_err = 1e9;
        for (int m = 0; m < g_res->nmode; m++)
        {
            const XRRModeInfo& mi = g_res->modes[m];
            if (mi.width != cur->width || mi.height != cur->height)
            {
                continue;
            }
            const double hz = mode_hz(mi);
            const double err = std::fabs(hz - 50.0);
            if (err < best_err)
            {
                best_err = err;
                best_id = mi.id;
            }
        }
        if (best_id == None || best_err > 0.75)
        {
            log_info("hz-lock: no ~50 Hz mode for %ux%u (now %.2f Hz)",
                     cur->width, cur->height, cur_hz);
            XRRFreeCrtcInfo(ci);
            continue;
        }
        if (best_id == ci->mode)
        {
            log_info("hz-lock: already %.2f Hz", cur_hz);
            XRRFreeCrtcInfo(ci);
            continue;
        }

        g_crtc = g_res->crtcs[c];
        g_saved_mode = ci->mode;
        g_saved_x = ci->x;
        g_saved_y = ci->y;
        g_saved_rot = ci->rotation;
        g_saved_noutput = ci->noutput;
        g_saved_outputs = static_cast<RROutput*>(std::malloc(
            static_cast<size_t>(ci->noutput) * sizeof(RROutput)));
        if (g_saved_outputs == nullptr)
        {
            XRRFreeCrtcInfo(ci);
            hz_lock_restore();
            return false;
        }
        for (int o = 0; o < ci->noutput; o++)
        {
            g_saved_outputs[o] = ci->outputs[o];
        }
        g_new_mode = best_id;

        const Status st = XRRSetCrtcConfig(g_dpy, g_res, g_crtc, CurrentTime,
                                           g_saved_x, g_saved_y, g_new_mode, g_saved_rot,
                                           g_saved_outputs, g_saved_noutput);
        XSync(g_dpy, False);
        XRRFreeCrtcInfo(ci);
        if (st != RRSetConfigSuccess)
        {
            log_warn("hz-lock: RandR set failed (%d)", static_cast<int>(st));
            hz_lock_restore();
            return false;
        }
        g_changed = true;
        const XRRModeInfo* nw = find_mode(g_res, g_new_mode);
        log_info("hz-lock: %ux%u %.2f Hz -> %.2f Hz (restore on quit)",
                 cur->width, cur->height, cur_hz, nw ? mode_hz(*nw) : 50.0);
        return true;
    }
    log_info("hz-lock: no active CRTC to retune");
    hz_lock_restore();
    return false;
}

void hz_lock_restore()
{
    if (g_changed && g_dpy != nullptr && g_res != nullptr && g_crtc != None
        && g_saved_mode != None && g_saved_outputs != nullptr)
    {
        const Status st = XRRSetCrtcConfig(g_dpy, g_res, g_crtc, CurrentTime,
                                           g_saved_x, g_saved_y, g_saved_mode, g_saved_rot,
                                           g_saved_outputs, g_saved_noutput);
        XSync(g_dpy, False);
        if (st == RRSetConfigSuccess)
        {
            log_info("hz-lock: restored previous refresh");
        }
        else
        {
            log_warn("hz-lock: restore failed (%d) — run: xrandr --output <out> --mode <old>",
                     static_cast<int>(st));
        }
    }
    g_changed = false;
    g_crtc = None;
    g_saved_mode = None;
    g_new_mode = None;
    if (g_saved_outputs != nullptr)
    {
        std::free(g_saved_outputs);
        g_saved_outputs = nullptr;
    }
    g_saved_noutput = 0;
    if (g_res != nullptr)
    {
        XRRFreeScreenResources(g_res);
        g_res = nullptr;
    }
    if (g_dpy != nullptr)
    {
        XCloseDisplay(g_dpy);
        g_dpy = nullptr;
    }
}
