//
// pfbneo turbo (auto-fire / 连发) support module.
//
// This module implements per-button turbo for the 8 main joystick buttons
// (A B X Y LB RB LT RT). The turbo settings are stored inside the *per-rom*
// config file (see PEMUConfig::loadGame / saveGame), so they can only be
// changed from the rom specific menu, and are automatically reloaded when
// the same rom is started again.
//
// IMPORTANT (include order / self-containment):
//  this header is self-contained on purpose. It only forward-declares the
//  external types it needs, so it can be included from retro_input.cpp and
//  pfbneo_config.h without pulling the whole pemu/skeleton include chain.
//  pfbneo_turbo.cpp is the only place that includes "skeleton/pemu.h" first
//  (which brings ss_api.h, c2d.h, pemu_config.h, ui_text_def.h and the
//  "using namespace ss_api" needed by pfbneo_utility.h).
//

#ifndef PFBNEO_TURBO_H
#define PFBNEO_TURBO_H

#include <cstdio>
#include <cstdarg>

// ---------------------------------------------------------------------------
// forward declarations (avoid heavy / non self-contained includes)
// ---------------------------------------------------------------------------
namespace ss_api {
    struct Game;
}

namespace c2d {
    namespace config {
        class Config;
    }
}

// ---------------------------------------------------------------------------
// SD card debug log control (requirement: macro switch, enabled by default)
// ---------------------------------------------------------------------------
#define TURBO_LOG_ENABLE 0

namespace pfbneo_turbo {

#define TURBO_BUTTON_COUNT 8
#define TURBO_RATE_COUNT   6

// id of the "TURBO" group inside the per-rom config.
// 0x5000 is used instead of CFG_ID_ROMS + N on purpose: this header must not
// depend on PEMUConfig (pemu_config.h is not self-contained). The value range
// 0x5000+ can never collide with the shared option ids (PEMUConfig::OptId
// is a small enum ending around 100).
#define TURBO_GROUP_ID     0x5000
#define TURBO_OPT_RATE     0x5001   // shared turbo rate (6 levels)
#define TURBO_OPT_A        0x5002   // +0 .. +7 = per button on/off
#define TURBO_OPT_B        0x5003
#define TURBO_OPT_X        0x5004
#define TURBO_OPT_Y        0x5005
#define TURBO_OPT_LB       0x5006
#define TURBO_OPT_RB       0x5007
#define TURBO_OPT_LT       0x5008
#define TURBO_OPT_RT       0x5009

    // the 8 controllable buttons, in menu display order: a b x y lb rb lt rt
    enum TurboButton {
        TURBO_BTN_A = 0,
        TURBO_BTN_B,
        TURBO_BTN_X,
        TURBO_BTN_Y,
        TURBO_BTN_LB,
        TURBO_BTN_RB,
        TURBO_BTN_LT,
        TURBO_BTN_RT
    };

    // --- lifecycle ---------------------------------------------------------
    void init();                              // open log / init defaults
    void onGameStart();                       // rom loaded (reset + log)
    void onGameStop();                        // rom stopped
    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig);

    // --- per-frame hooks (called from retro_input.cpp) ---------------------
    void tick();                              // advance turbo timing (1x/frame)
    int  apply(unsigned int id, int rawState); // apply turbo to a joystick query

    // --- helpers ------------------------------------------------------------
    int         idToSlot(unsigned int id);    // retro joystick id -> 0..7, -1 if not turbo-able
    unsigned int getButtonId(TurboButton b);  // retro joystick id of slot b
    const char *getButtonName(TurboButton b);      // config key name, e.g. "TURBO_A"
    const char *getButtonDisplayName(TurboButton b);
    int         getRateIndex();
    const char *getRateName(int index);
    unsigned int getSupportedMask();          // bitmask of buttons used by the current rom
    bool        isSupported(TurboButton b);

    // --- SD card log ---------------------------------------------------------
    void turboLog(const char *fmt, ...);
}

#if TURBO_LOG_ENABLE
#define TURBO_LOG(...) pfbneo_turbo::turboLog(__VA_ARGS__)
#else
#define TURBO_LOG(...) ((void) 0)
#endif

// exported from retro_input.cpp (input mapping layer)
unsigned PfbneoTurboGetUsedMask(bool remap);
bool PfbneoTurboInputInitialized();

#endif //PFBNEO_TURBO_H
