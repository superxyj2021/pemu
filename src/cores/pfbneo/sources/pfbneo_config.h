//
// Created by cpasjuste on 29/05/18.
//

#ifndef PFBN_CONFIG_H
#define PFBN_CONFIG_H

#include "skeleton/pemu_config.h"
#include "pfbneo_utility.h"
#include "burner.h"
#include "pfbneo_keybind.h"
#include "fbneo/pfbneo_dip.h"
#include "fbneo/pfbneo_memcard.h"
#include "fbneo/pfbneo_ips.h"
#include "fbneo/pfbneo_movelist.h"
#include "fbneo/pfbneo_cheat.h"

class PFBAConfig final : public PEMUConfig {
public:
    PFBAConfig(Renderer *renderer, const int version);

    std::vector<int> getCoreHiddenOptionToEnable() override {
        return {
                UI_FILTER_CLONES,
                UI_FILTER_SYSTEM
        };
    }

    std::string getCoreVersion() override {
        return "fbneo: " + std::string(szAppBurnVer);
    }

    std::vector<std::string> getCoreSupportedExt() override {
        return {".zip"};
    }

    // pfbneo keybind: add the per-rom "KEYBIND" config group
    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig) override {
        pfbneo_keybind::onGameConfigCreate(game, gameConfig);
        // pfbneo DIP: add the per-rom "DIP" config group
        pfbneo_dip::onGameConfigCreate(game, gameConfig);
        // pfbneo memcard: add the per-rom "MEMCARD" config group
        pfbneo_memcard::onGameConfigCreate(game, gameConfig);
        // pfbneo IPS: add the per-rom "IPS" config group
        pfbneo_ips::onGameConfigCreate(game, gameConfig);
        // pfbneo move list: detect the current game's command.dat entry
        pfbneo_movelist::onGameConfigCreate(game, gameConfig);
    }

    // rebuild the lazy lookup tables right after a rom scan (startup scan and
    // every manual rescan go through RomList::build -> this hook), so neither
    // the first menu open (command.dat) nor the first game load (cheat.dat
    // index) has to build them on the spot
    void onRomScanCompleted() override {
        pfbneo_movelist::preload();
        pfbneo_cheat::preload();
    }
};

#endif //PFBN_CONFIG_H
