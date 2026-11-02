//
// pfbneo key settings (按键设置) support module.
//
// Replaces the old pfbneo_turbo module. Provides, for every game, a per-rom
// "KEYBIND" config group with one row per logical key (player driver inputs in
// driver order + the combo macros ported from the Windows build):
//   - a bind option: physical button id (SDL controller button / trigger
//     axis+100), -1 = unbound;
//   - a turbo option (only for rows where turbo makes sense, i.e. everything
//     except directions and coin/select/start).
// A raw per-input binding overrides the default JOY mapping for that game
// input only; it applies to the input's own player port, and the same config
// is shared by all connected controllers (never per-controller).
//
// Runtime: query() is called from CinpState() and returns -1 when the input
// has no custom binding (default path is used), otherwise the pressed state
// (with the per-input turbo pattern applied). tick() advances the per-input
// turbo counters, once per emulated frame, from InputMake().
//
// IMPORTANT (include order / self-containment): same rules as the former
// pfbneo_turbo.h - only forward-declare what is needed so the header can be
// included from retro_input.cpp and pfbneo_config.h without pulling the whole
// pemu/skeleton include chain.
//

#ifndef PFBNEO_KEYBIND_H
#define PFBNEO_KEYBIND_H

#include <string>

namespace ss_api {
    struct Game;
}

namespace c2d {
    namespace config {
        class Config;
        class Option;
    }
}

namespace pfbneo_keybind {

// id of the "KEYBIND" group inside the per-rom config.
// 0x5100+ never collides with the shared option ids (PEMUConfig::OptId is a
// small enum ending around 100) nor with the old turbo ids (0x5000+).
#define KEYBIND_GROUP_ID   0x5100
#define KEYBIND_OPT_RATE   0x5101   // shared turbo rate (6 levels)
#define KEYBIND_OPT_BIND   0x5102   // +2*row: physical key id (-1 = use default, -2 = cleared)
#define KEYBIND_OPT_TURBO  0x5103   // +2*row: turbo on/off (turbo-allowed rows only)

// bind-option value meaning "explicitly cleared": the default JOY binding is
// suppressed and the input stays disabled until a new key is captured (A).
// -1 = no custom binding (use the default JOY mapping), physical ids are >= 0.
#define KEYBIND_BIND_CLEARED  (-2)

    // --- lifecycle (called from the pfbneo core/config layers) -------------
    void init();                                   // init defaults
    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig);
    void onGameStart();                            // game loaded + input mapped
    void onGameStop();                             // game stopped

    // --- per-frame hooks (called from retro_input.cpp) ---------------------
    void tick();                                   // advance turbo timing (1x/frame)
    int  query(int nCode);                         // -1 = no override, else 0/1

    // --- row access for the key dialog -------------------------------------
    int         getRowCount();
    const char *getRowName(int row);
    int         getDefaultKey(int row);              // physical key id of the
    // default JOY binding for a row (-1 when none/macro)
    c2d::config::Option *getBindOption(int row);
    c2d::config::Option *getTurboOption(int row);  // nullptr when turbo not allowed
    bool        isTurboAllowed(int row);
    c2d::config::Option *getRateOption();
    int         getEffectiveKey(int row);            // custom binding, else the
    // default JOY binding; -1 when the row is cleared or has no binding at all

    // Chinese display name for a driver/macro description ("P1 Coin" -> "投币").
    // strips the "P1 "/"P2 " prefix and translates the remainder.
    std::string getDisplayName(const std::string &desc);
}

#endif // PFBNEO_KEYBIND_H