//
// pfbneo move list (出招表) support module.
//
// Port of the FBNeo Windows game-info "Commands" tab: parses the MAME-style
// command.dat (https://www.progettosnaps.net/command/) and exposes the move
// list of the current game to a single-page accordion dialog.
//
// File layout (the Windows frontend keeps command.dat under support/commands/,
// but many setups drop it straight into the data path root - both are read,
// <data path>/commands/command.dat first, then <data path>/command.dat).
// Format (MAME command.dat convention, parsed like win32 gameinfo.cpp):
//   $info=<drvname1>,<drvname2>      section header, comma separated
//   $cmd                             start of a group
//   [ 标题 ]                          group title (brackets stripped)
//   <command text>                   lines that follow belong to the group
//   $end                             section end
// The chinese command lists group the moves into "$cmd" blocks; the classic
// progettosnaps format instead marks every move with a "- " line:
//   - <move name>
//   <command input>
// Both styles are supported and may mix inside one section. Free text before
// the first group of a section (game title / intro notes) is skipped.
// Expand state is session-local (kept by the dialog), nothing is persisted.
//

#ifndef PFBNEO_MOVELIST_H
#define PFBNEO_MOVELIST_H

#include <string>

namespace ss_api {
    struct Game;
}

namespace c2d {
    namespace config {
        class Config;
    }
}

namespace pfbneo_movelist {

    // --- lifecycle (called from the pfbneo core/config layers) -------------
    void init();
    // (re)build the whole-file command.dat lookup table now (called after the
    // startup rom scan and after every rescan, so the first menu open never
    // has to parse it on the spot)
    void preload();
    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig);
    void onGameStop();

    // --- feature detection --------------------------------------------------
    bool hasMovelist();             // command.dat has an entry for this game?
    const char *getRomName();       // romset name used for the match

    // --- move groups --------------------------------------------------------
    int  getMoveCount();            // number of move groups (incl. intro)
    const char *getMoveName(int idx);
    int  getCommandCount(int idx);  // lines under the group
    const char *getCommand(int idx, int cmdIdx);
}

#endif // PFBNEO_MOVELIST_H