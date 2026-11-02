//
// pfbneo DIP switch (DIP 开关) support module.
//
// Provides, for every game, a per-rom "DIP" config group with one array
// option per DIP switch (0xFE/0xFD group header in the driver's DIP list):
// the option's values are the switch's option texts (Chinese display names
// via the "Chinese (Simplified)_dips.glt" table, English fallback) and the
// selected index is the persisted setting.
//
// The factory default of every switch is derived deterministically from the
// driver's 0xFF "default override" entries applied to 0, matching what
// InpDIPSWResetDIPs() would produce for an all-zero DIP block.
//
// Runtime: the selected option is applied to the running GameInp[] DIP
// constant (GameInp[nInput + nDIPOffset].Input.Constant.nConst) at game
// start (onGameStart, right after InputInit) and live from the DIP dialog
// (applyRow). Persistence uses the standard per-rom config save/restore.
//
// IMPORTANT (include order / self-containment): same rules as pfbneo_keybind.h
// - only forward-declare what is needed so the header can be included from
// retro_input.cpp and pfbneo_config.h without pulling the whole pemu/skeleton
// include chain.
//

#ifndef PFBNEO_DIP_H
#define PFBNEO_DIP_H

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

namespace pfbneo_dip {

// id of the "DIP" group inside the per-rom config.
// 0x5300+ never collides with the shared option ids (PEMUConfig::OptId),
// the keybind ids (0x5100+) nor the old turbo ids (0x5000+).
#define DIP_GROUP_ID   0x5300
#define DIP_OPT_BASE   0x5301   // + row: one array option per DIP switch

    // --- lifecycle (called from the pfbneo core/config layers) -------------
    void init();                                   // init defaults
    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig);
    void onGameStart();                            // apply saved values (after InputInit)
    void onGameStop();                             // game stopped

    // --- row access for the DIP dialog --------------------------------------
    int  getRowCount();
    const char *getRowName(int row);               // Chinese switch name
    c2d::config::Option *getValueOption(int row);  // array option (index = selection)
    bool isChanged(int row);                       // selection != factory default
    int  getDefaultIndex(int row);
    // live-apply the current selection of `row` into the running GameInp[]
    // DIP constants (no-op when no game is mapped)
    void applyRow(int row);

    // Chinese display name for a DIP text (table lookup, English fallback).
    std::string getDisplayName(const std::string &text);
}

#endif // PFBNEO_DIP_H