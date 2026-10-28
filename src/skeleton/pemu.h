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

// donate.png integrity flag (set once at startup by the core's main()).
// when the CRC32 of romfs:/donate/donate.png does not match the expected
// value the file is considered tampered: opening any rom is blocked and the
// +/- menus only show the "退出" (quit) entry.
extern bool g_donateValid;

using namespace c2d;
using namespace pemu;
using namespace ss_api;

#endif //PEMU_H
