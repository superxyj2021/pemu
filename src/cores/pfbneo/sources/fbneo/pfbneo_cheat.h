//
// pfbneo cheat (金手指) support.
//
// Loads existing FBA-style cheat files from the sd card:
//     <data path>/cheats/<rom zip name>.ini
//
// File format (subset of the FBA .ini cheat format):
//     // comment
//     cheat "Cheat Name"
//     type 0
//     default 0
//     0 "Disabled" 0, 0, 0
//     1 "Enabled"  0, 0x200001, 0xFF
//     ...
//
//   option line:  <index> "<option name>"  <cpu>,<address>,<value>[ , <cpu>,<address>,<value> ...]
//   (multiple address groups per option are supported; size=1 byte, mask=0xFF)
//
// IMPORTANT (current design): cheat state is RUNTIME-ONLY.
//   - the cheat menu entry only exists in the rom menu (game running);
//   - selections are NOT persisted to any cfg file, they only live in memory
//     and are cleared when the game stops;
//   - nothing is added to the global menu or to the per-rom cfg.
//

#ifndef PFBN_CHEAT_H
#define PFBN_CHEAT_H

#include <string>
#include <vector>

#include "cross2d/skeleton/io.h"

// forward declarations (full definitions come from "skeleton/pemu.h")
namespace ss_api {
    class Game;
}

namespace pfbneo_cheat {

    // where the cheats of the current game actually came from. lookup order:
    //   own ini > own dat (plus parent dat in the same single pass) > parent ini
    enum class CheatSource {
        None,       // no cheat file found at all
        Ini,        // own <drvname>.ini
        Dat,        // own section inside the shared .dat database
        ParentIni,  // parent (clone of) rom's <parent>.ini
        ParentDat   // parent rom's section inside the shared .dat database
    };

    // parse <cheats>/<rom>.ini and rebuild the core pCheatInfo list.
    // MUST be called AFTER BurnDrvInit() (cpu registers are registered then;
    // the core frees pCheatInfo in CheatExit() when the driver exits).
    void load(const ss_api::Game &game, c2d::Io *io);

    // which source the loaded cheats came from (filled by load(); used by the
    // dialog to annotate the title when cheats were inherited from the parent)
    CheatSource getSource();
    const std::string &getSourceRom();

    // select one option of a cheat (dialog): apply immediately to the running
    // emulation. runtime-only, no cfg persistence.
    void selectOption(int cheatIndex, int optionIndex);

    // reset runtime state (called on emulation stop)
    void reset();

    // accessors for the cheat dialog
    int getCheatCount();
    const char *getCheatName(int cheatIndex);
    int getOptionCount(int cheatIndex);
    const char *getOptionName(int cheatIndex, int optionIndex);
    int getSelectedOption(int cheatIndex);

} // namespace pfbneo_cheat

#endif // PFBN_CHEAT_H
