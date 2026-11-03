/* SPDX-License-Identifier: GPL-2.0-or-later
 * Desktop-size evidence for the Glide probes. An open Glide context owns the
 * display: by default the desktop takes the Glide resolution (OpenGLid.ini
 * InitFullScreen=0 keeps a window), and every close must restore it.
 * Include after the probe's Record(). */
#pragma once

struct DesktopSize {
    int width, height;
};
inline bool SameDesktop(DesktopSize a, DesktopSize b) {
    return a.width == b.width && a.height == b.height;
}
/* Records "DISPLAY <label> <width>x<height>" and returns the size. */
inline DesktopSize RecordDesktop(const char *label) {
    const DesktopSize d = {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    char line[96];
    wsprintfA(line, "DISPLAY %s %dx%d", label, d.width, d.height);
    Record(line);
    return d;
}
