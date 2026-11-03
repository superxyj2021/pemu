//
// pfbneo IPS patch (IPS补丁) support module.
//
// Port of the FBNeo Windows IPS patching feature. The core side (burn/load.cpp
// calling IpsApplyPatches() during BurnLoadRom, and the drivers reading
// bDoIpsPatch / nIpsDrvDefine / nIpsMemExpLen to grow rom segments / skip
// protections) is already compiled in; this module replaces the disabled
// burner/sdl/ips_manager.cpp stubs with the real implementation.
//
// Layout (mirrors the Windows support/ips/ tree, but under the nro data path):
//   <data path>/ips/<drivername>/*.dat
// Each .dat contains patch-entry lines
//   "rom name.bin" "patch file.ips" [IPS_OFFSET_xxx] [crc(abcd1234)]
// followed by an info section with multi-language descriptions
//   [zh_CN] ... [en_US] ...   (the dialog shows the Chinese description)
// and optional #define IPS_*_EXPAND / IPS_*_HACK lines that grow rom buffers
// and enable driver hacks, plus #include "other.dat" (early/last) ordering.
//
// The enabled state of every .dat is stored in a per-rom "IPS" config group
// (one bool option per file); it is applied right before DrvInit loads the
// roms. Like the Windows manager, multiple .dat files can be enabled at once:
// they are applied in order and any overlapping byte is overwritten by the
// later patch, while the *EXPAND defines are max'd (never summed).
//

#ifndef PFBNEO_IPS_H
#define PFBNEO_IPS_H

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

namespace pfbneo_ips {

// id of the "IPS" group inside the per-rom config. 0x5400+ does not collide
// with the shared ids nor with KEYBIND (0x5100) / MEMCARD (0x5200) / DIP (0x5300).
#define IPS_GROUP_ID   0x5400
#define IPS_OPT_BASE   0x5401   // +dat: enabled (bool)

    // --- lifecycle (called from the pfbneo core/config layers) -------------
    void init();
    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig);
    void applyEnabled();        // must run right before DrvInit (rom loading)
    void onGameStop();

    // --- feature detection --------------------------------------------------
    bool hasIps();              // current game has .dat files? (menu item gate)
    const char *getRomName();   // romset name (ips directory name)

    // --- dialog row model ---------------------------------------------------
    // rows = category headers (non-selectable) + one row per .dat file
    int  getRowCount();
    bool isCategoryRow(int row);
    const char *getRowName(int row);
    bool isRowEnabled(int row); // false for category rows
    void toggleRow(int row);    // flips the enabled state of a .dat row
}

#endif // PFBNEO_IPS_H