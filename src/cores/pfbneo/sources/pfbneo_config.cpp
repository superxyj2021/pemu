//
// Created by cpasjuste on 29/05/18.
//

#include <string>
#include "skeleton/pemu.h"
#include "pfbneo_config.h"
#include "fbneo/pfbneo_memcard.h"
#include "fbneo/pfbneo_ips.h"
#include "fbneo/pfbneo_movelist.h"

using namespace c2d;
using namespace pemu;

#define C2D_CONFIG_RESTART_EMU_NEEDED "YOU NEED TO RESTART EMULATION AFTER CHANGING THIS OPTION"

PFBAConfig::PFBAConfig(Renderer *renderer, const int version) : PEMUConfig(renderer, "PFBNEO", version) {
    printf("PFBNConfig(%s, v%i)\n", getPath().c_str(), version);

    // pfbneo keybind: init defaults
    pfbneo_keybind::init();

    // pfbneo DIP: init defaults
    pfbneo_dip::init();

    // pfbneo memcard: init defaults
    pfbneo_memcard::init();

    // pfbneo IPS: init defaults
    pfbneo_ips::init();

    // pfbneo move list: init defaults
    pfbneo_movelist::init();

    c2d::Io *io = renderer->getIo();

    // unhide core options (UI_FILTER_SYSTEM / UI_FILTER_CLONES) here, not just
    // in RomList::build(): the rom list ui is now attached before the first
    // scan runs, and it reads the flags at startup to decide whether to show
    // the system text and enable the LT/RT system switching.
    for (const auto &id: getCoreHiddenOptionToEnable()) {
        get(id)->setFlags(0);
    }

    /// MAIN OPTIONS
    // hide menu entries that don't make sense on this build: only the default
    // skin is shipped, and font scaling is a debug tweak. hidden (not removed)
    // because skin.cpp reads both options at startup.
    get(UI_SKIN)->setFlags(PEMUConfig::Flags::HIDDEN);
    get(UI_FONT_SCALING)->setFlags(PEMUConfig::Flags::HIDDEN);

    /// ROMS OPTIONS
    const auto group = getGroup(EMULATION);
    if (!group) {
        printf("PFBNConfig: error, group not found (MENU_ROM_OPTIONS)\n");
        return;
    }
    group->addOption(
            {"FORCE_60HZ", {"OFF", "ON"}, 1, EMU_FORCE_60HZ, TEXT_MSG_RESTART_NEEDED, TEXT_MENU_FORCE_60HZ});
    group->addOption({"AUDIO_FREQUENCY", {"11025", "22050", "32000", "44100", "48000"},
                      3, EMU_AUDIO_FREQ, TEXT_MSG_RESTART_NEEDED, TEXT_MENU_AUDIO_FREQUENCY});
    group->addOption({"AUDIO_INTERPOLATION", {"0", "1", "3"},
                      2, EMU_AUDIO_INTERPOLATION, TEXT_MSG_RESTART_NEEDED, TEXT_MENU_AUDIO_INTERPOLATION});
    // FM synthesis chips (YM2612/YM3526): value 3 runs the FM core at the real
    // hardware clock and resamples (best quality, high CPU cost), 0/1 runs it
    // directly at the output rate (fast). default 0 = performance priority.
    group->addOption({"AUDIO_FM_INTERPOLATION", {"0", "1", "3"},
                      0, EMU_AUDIO_FMINTERPOLATION, TEXT_MSG_AUDIO_FM_INTERPOLATION, TEXT_MENU_AUDIO_FM_INTERPOLATION});
#ifdef __VITA__
    group->addOption({"ROTATION", {"OFF", "ON", "FLIP", "CAB MODE"},
                      1, EMU_ROTATION, TEXT_MSG_RESTART_NEEDED, TEXT_MENU_ROTATION});
#else
    group->addOption({"ROTATION", {"OFF", "ON", "FLIP"},
                      1, EMU_ROTATION, TEXT_MSG_RESTART_NEEDED, TEXT_MENU_ROTATION});
#endif
    // pfbneo bezel (遮罩/边框): the option is created in the skeleton
    // EMULATION group ABOVE the scaling option (hidden there); unhide it.
    // bezel and scaling are linked (see applyBezel / the EMU_SCALING menu
    // handler): bezel on forces fit-screen scaling, a non-fit scaling turns
    // the bezel off.
    get(EMU_BEZEL)->setFlags(0);
    group->addOption(
            {"NEOBIOS", {"UniBIOS 4.0", "UniBIOS 3.3", "UniBIOS 3.2", "UniBIOS 3.1",
                         "MVS 亚欧版 V6S1", "MVS 亚欧版 V5S1", "MVS 亚欧版 V3S4",
                         "MVS 美版 V5S2", "MVS 美版 V5S4", "MVS 美版 V5S6", "MVS 日版 V6",
                         "MVS 日版 V5", "MVS 日版 V3S4", "MVS 日版 J3", "AES 亚洲版",
                         "AES 日版", "MV-1C 亚洲版", "MV-1C 日版", "NEOGEO CD 主机 V6", "开发机"},
             0, EMU_NEOBIOS, TEXT_MSG_RESTART_NEEDED, TEXT_MENU_NEOBIOS});
#if defined(__PFBA_ARM__) || defined(__SWITCH__)
#ifdef __PFBA_ARM__
    // do not use unibios as default on vita for cyclone asm compatibility
    group->getOption(EMU_NEOBIOS)->setArrayIndex(4);
#endif
    // merged frameskip option: OFF / 自动 (auto) / 1..9 (manual skip level).
    // read live every frame, no restart needed.
    group->addOption({"FRAMESKIP", {"OFF", "自动", "1", "2", "3", "4", "5", "6", "7", "8", "9"},
                      1, EMU_FRAMESKIP, "", TEXT_MENU_FRAMESKIP});
#ifdef __SWITCH__
    // emulation performance mode: 0 = power save (stock clocks), 1 = full speed
    // (cpu max + gpu 460mhz on game load). default is full speed.
    // placed right after FRAMESKIP so it appears below it in the menu.
    group->addOption({
        "PERF_MODE", {TEXT_MENU_EMU_PERF_SAVE, TEXT_MENU_EMU_PERF_FULL}, 0,
        EMU_PERF_MODE, "", TEXT_MENU_EMU_PERF
    });
#endif
#endif

#if defined(__PS4__) || defined(ANDROID) || defined(__SWITCH__)
    // force 48000hz audio output (matches the switch/ps4/android audio hw,
    // avoids an extra resample step from the 44100 default)
    get(EMU_AUDIO_FREQ)->setArrayIndex(4);
    get(EMU_AUDIO_FREQ)->setFlags(PEMUConfig::Flags::HIDDEN);
#endif

    // "romlist.cpp" (RomList::build) will also reload config, but we need new roms paths
    PEMUConfig::load();

#if defined(__PS4__) || defined(ANDROID) || defined(__SWITCH__)
    // re-apply the forced 48000hz AFTER the saved config is loaded: the saved
    // config.cfg value (e.g. an old 44100) must not override the platform
    // default, the audio hw never needs anything else
    get(EMU_AUDIO_FREQ)->setArrayIndex(4);
#endif

    // add custom rom path 
    addRomPath("ARCADE", io->getDataPath() + "roms/arcade/", {HARDWARE_PREFIX_ARCADE, 0, "Arcade"});
#ifndef __PFBN_LIGHT__
    addRomPath("channelf", io->getDataPath() + "roms/channelf/", {HARDWARE_PREFIX_CHANNELF, 0, "Fairchild Channel F"});
    addRomPath("coleco", io->getDataPath() + "roms/coleco/", {HARDWARE_PREFIX_COLECO, 0, "ColecoVision"});
    addRomPath("fds", io->getDataPath() + "roms/fds/", {HARDWARE_PREFIX_FDS, 0, "Famicom Disk System"});
    addRomPath("gamegear", io->getDataPath() + "roms/gamegear/", {HARDWARE_PREFIX_SEGA_GAME_GEAR, 0, "Sega Game Gear"});
    addRomPath("megadrive", io->getDataPath() + "roms/megadrive/", {HARDWARE_PREFIX_SEGA_MEGADRIVE, 0, "Sega Mega Drive"});
    addRomPath("msx", io->getDataPath() + "roms/msx/", {HARDWARE_PREFIX_MSX, 0, "MSX"});
    addRomPath("nes", io->getDataPath() + "roms/nes/", {HARDWARE_PREFIX_NES, 0, "Nintendo Entertainment System"});
    addRomPath("ngp", io->getDataPath() + "roms/ngp/", {HARDWARE_PREFIX_NGP, 0, "Neo Geo Pocket"});
    addRomPath("pce", io->getDataPath() + "roms/pce/", {HARDWARE_PCENGINE_PCENGINE, 0, "PC Engine"});
    addRomPath("sg1000", io->getDataPath() + "roms/sg1000/", {HARDWARE_PREFIX_SEGA_SG1000, 0, "Sega SG-1000"});
    addRomPath("sgx", io->getDataPath() + "roms/sgx/", {HARDWARE_PCENGINE_SGX, 0, "PC Engine SuperGrafx"});
    addRomPath("sms", io->getDataPath() + "roms/sms/", {HARDWARE_PREFIX_SEGA_MASTER_SYSTEM, 0, "Sega Master System"});
    addRomPath("spectrum", io->getDataPath() + "roms/spectrum/", {HARDWARE_PREFIX_SPECTRUM, 0, "ZX Spectrum"});
    addRomPath("tg16", io->getDataPath() + "roms/tg16/", {HARDWARE_PCENGINE_TG16, 0, "PC Engine TurboGrafx-16"});
    addRomPath("snes", io->getDataPath() + "roms/snes/", {HARDWARE_PREFIX_SNES, 0, "Super Nintendo Entertainment System"});
    addRomPath("gba", io->getDataPath() + "roms/gba/", {HARDWARE_PREFIX_GBA, 0, "Game Boy Advance"});	
#endif

    // save newly added roms paths
    PEMUConfig::save();

    // create roms paths if needed
    const auto paths = getRomPaths();
    for (const auto &path: paths) {
        io->create(path.path);
    }
}
