//
// Created by cpasjuste on 28/05/18.
//

#ifndef PEMU_H
#define PEMU_H

#include "ss_api.h"
#include "cross2d/c2d.h"

#ifdef __MPV__

#include "mpv.h"
#include "mpv_texture.h"

#endif

#include "romlist.h"
#include "skin.h"
#include "ui_video.h"
#include "pemu_config.h"

#include "skin/SkinnedRectangle.h"
#include "skin/SkinnedText.h"

#include "ui_listbox.h"
#include "ui_highlight.h"
#include "ui_menu.h"
#include "ui_emu.h"
#include "ui_romlist.h"
#include "ui_romlist_rominfo.h"
#include "ui_progressbox.h"
#include "ui_menu_state.h"
#include "ui_status_box.h"
#include "ui_main.h"
#include "ui_help.h"

#include "ui_text_def.h"

// master switch for ALL pfbneo debug logs (cheat / turbo / emu / menu).
// 0 = logs compiled out (release), 1 = debug logs written to /switch/*.log.
#ifndef PEMU_ENABLE_DEBUG_LOG
#define PEMU_ENABLE_DEBUG_LOG 0
#endif

// internal state flag (set by the core during startup and by the probe
// below); when cleared, opening roms is blocked and the +/- menus only show
// the "退出" (quit) entry.
extern bool g_qq7f;

// rolling digest used by the embedded-image sanity probes (NOT a standard
// CRC32; a deliberately mixed shift/xor/multiply sequence). callers apply
// it to buffer_img and compare against the reference halves.
static inline unsigned long long x7fq(const unsigned char *k, unsigned long long n) {
    unsigned long long a = 0x2b6e2f4d5c3a1f89ULL;
    unsigned long long b = 0x9e3779b97f4a7c15ULL;
    for (unsigned long long i = 0; i < n; i++) {
        a ^= k[i];
        a = a * 0x5851f42d4c957f2dULL + b;
        a = (a << 37) | (a >> 27);
        b ^= a >> 17;
        b = b * 0x14057b7ef767814fULL + 0x21ULL;
    }
    return (a ^ (b << 33)) ^ (b >> 31);
}

using namespace c2d;
using namespace pemu;
using namespace ss_api;

#endif //PEMU_H
