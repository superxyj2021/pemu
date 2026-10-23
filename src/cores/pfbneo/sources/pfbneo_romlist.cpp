//
// Created by cpasjuste on 28/09/23.
//

#include "skeleton/pemu.h"
#include "pfbneo_romlist.h"
#include "pfbneo_utility.h"
#include "burner.h"

using namespace c2d;
using namespace pemu;

void PFBNRomList::build(const ss_api::GameList::GameAddedCb &cb) {
    float time_start = ui->getElapsedTime().asSeconds();
    // build rom list
    RomList::build([this](Game *game) {
        auto cfgSys = game->system;
        PFBNEOUtility::GameInfo gameInfo = PFBNEOUtility::getGameInfo(*game, ui->getIo()->getRomFsPath() + "langs/gamelist.bin");
#if 0
        printf("game: %s, path: %s, fbneo system: %s (0x%08x), core system: %s (0x%08x)\n",
               game->path.c_str(), game->romsPath.c_str(),
               gameInfo.system.c_str(), gameInfo.systemId, cfgSys.name.c_str(), cfgSys.id);
#endif
        // replace gamename
        game->name = gameInfo.name;

        // replace editor/developer
        if ((game->editor.name.empty() || game->editor.name == "UNKNOWN")
            && !game->developer.name.empty() && game->developer.name != "UNKNOWN") {
            game->editor.name = game->developer.name;
            game->editor.id = game->developer.id;
        } else if ((game->developer.name.empty() || game->developer.name == "UNKNOWN")
                   && !game->editor.name.empty() && game->editor.name != "UNKNOWN") {
            game->developer.name = game->editor.name;
            game->developer.id = game->editor.id;
        }
        if (!gameInfo.manufacturer.empty() && gameInfo.manufacturer != "<unknown>") {
            //if (game->editor.name.empty() || game->editor.name == "UNKNOWN") {
                game->editor.name = gameInfo.manufacturer;
            //}
            //if (game->developer.name.empty() || game->developer.name == "UNKNOWN") {
                game->developer.name = gameInfo.manufacturer;
            //}
        }

        // replace system for arcade roms
        if (cfgSys.id == HARDWARE_PREFIX_ARCADE // is an arcade rom (located in arcade roms folder)
            && !gameInfo.sysName.empty() // system was found in fbneo database
            && gameInfo.sysId != 0x0a004822 // not "sbubsm.zip" (Sega Megadrive)
            && gameInfo.sysId != 0x0a004800 // not "topshoot.zip" (Sega Megadrive)
                ) {
            game->system = {gameInfo.sysId, HARDWARE_PREFIX_ARCADE, gameInfo.sysName};
            // add the system to system list if not added yet
            if (!gameList->systemList.find(game->system.id)) {
                gameList->systemList.systems.emplace_back(game->system);
            }
        }

        // replace parent/clone
        if (!gameInfo.parent.empty()) {
            game->cloneOf = gameInfo.parent + ".zip";
        }
		
        // replace players
        game->playersInt = gameInfo.players;
		if (game->playersInt > 1)
			game->players = "1-" + std::to_string(game->playersInt);
		else
			game->players = std::to_string(game->playersInt);
		
        // replace date
        if (!gameInfo.date.empty()) {
            game->date = gameInfo.date;
        }	
		
        // replace genre
		game->genre.id = gameInfo.genre;
		game->genre.name.clear();

		// 辅助 lambda：命中则追加名称，用 " / " 分隔, 避免有些游戏有多个类型标签而变为unknown
		auto appendGenre = [&game, &gameInfo](int flag, const char* name) {
			if (gameInfo.genre & flag) {
				if (!game->genre.name.empty()) {
					game->genre.name += " / ";
				}
				game->genre.name += name;
			}
		};

		appendGenre(GBF_HORSHOOT,      TEXT_GBF_HORSHOOT);
		appendGenre(GBF_VERSHOOT,      TEXT_GBF_VERSHOOT);
		appendGenre(GBF_SCRFIGHT,      TEXT_GBF_SCRFIGHT);
		appendGenre(GBF_VSFIGHT,       TEXT_GBF_VSFIGHT);
		appendGenre(GBF_BIOS,          TEXT_GBF_BIOS);
		appendGenre(GBF_BREAKOUT,      TEXT_GBF_BREAKOUT);
		appendGenre(GBF_CASINO,        TEXT_GBF_CASINO);
		appendGenre(GBF_BALLPADDLE,    TEXT_GBF_BALLPADDLE);
		appendGenre(GBF_MAZE,          TEXT_GBF_MAZE);
		appendGenre(GBF_MINIGAMES,     TEXT_GBF_MINIGAMES);
		appendGenre(GBF_PINBALL,       TEXT_GBF_PINBALL);
		appendGenre(GBF_PLATFORM,      TEXT_GBF_PLATFORM);
		appendGenre(GBF_PUZZLE,        TEXT_GBF_PUZZLE);
		appendGenre(GBF_QUIZ,          TEXT_GBF_QUIZ);
		appendGenre(GBF_SPORTSMISC,    TEXT_GBF_SPORTSMISC);
		appendGenre(GBF_SPORTSFOOTBALL,TEXT_GBF_SPORTSFOOTBALL);
		appendGenre(GBF_MISC,          TEXT_GBF_MISC);
		appendGenre(GBF_MAHJONG,       TEXT_GBF_MAHJONG);
		appendGenre(GBF_RACING,        TEXT_GBF_RACING);
		appendGenre(GBF_SHOOT,         TEXT_GBF_SHOOT);
		appendGenre(GBF_MULTISHOOT,    TEXT_GBF_MULTISHOOT);
		appendGenre(GBF_ACTION,        TEXT_GBF_ACTION);
		appendGenre(GBF_RUNGUN,        TEXT_GBF_RUNGUN);
		appendGenre(GBF_STRATEGY,      TEXT_GBF_STRATEGY);
		appendGenre(GBF_VECTOR,        TEXT_GBF_VECTOR);
		appendGenre(GBF_RPG,           TEXT_GBF_RPG);
		appendGenre(GBF_SIM,           TEXT_GBF_SIM);
		appendGenre(GBF_ADV,           TEXT_GBF_ADV);
		appendGenre(GBF_CARD,          TEXT_GBF_CARD);
		appendGenre(GBF_BOARD,         TEXT_GBF_BOARD);

		// 没有任何分类命中时，回退到 unknown
		if (game->genre.name.empty()) {
			game->genre.name = TEXT_UNKNOWN;
		}

		// 检查媒体文件信息是否填充，如果没有，强制写一个对应文件名的值当作默认值。
		// 检查是否已存在 type 为 "video" 的媒体
		std::string zipName = c2d::Utility::removeExt(game->path);
		
		auto& m_list = game->medias;

		if (m_list.empty()) {
			game->medias.push_back({"./media/images/" + zipName + ".png", "mixrbv2", "png"});
			game->medias.push_back({"./media/videos/" + zipName + ".mp4", "video", "mp4"});
		} else {
			auto hasVideo = std::find_if(
				m_list.begin(),
				m_list.end(),
				[](const ss_api::Game::Media& m) { return m.type == "video"; }
			) != m_list.end();

			auto hasImage = std::find_if(
				m_list.begin(),
				m_list.end(),
				[](const ss_api::Game::Media& m) { return m.type == "mixrbv2"; }
			) != m_list.end();

			if (!hasVideo) {
				game->medias.push_back({"./media/videos/" + zipName + ".mp4", "video", "mp4"});
			}

			if (!hasImage) {
				game->medias.push_back({"./media/images/" + zipName + ".png", "mixrbv2", "png"});
			}
		}
    });
	
    printf("PFBNRomList::build: removing HARDWARE_PREFIX_ARCADE...\n");
    // remove custom "ARCADE" system added from "RomList::build"
    gameList->systemList.remove(HARDWARE_PREFIX_ARCADE);

    printf("PFBNRomList::build: sorting systems...\n");
    // sort systems
    std::sort(gameList->systemList.systems.begin(), gameList->systemList.systems.end(), Api::sortSystemByName);

    // update system filtering option
    printf("PFBNRomList::build: updating UI_FILTER_SYSTEM...\n");
    auto opt = ui->getConfig()->get(PEMUConfig::OptId::UI_FILTER_SYSTEM);
    if (opt != nullptr) {
        opt->setArray(gameList->systemList.getNames());
    } else {
        printf("PFBNRomList::build: updating UI_FILTER_SYSTEM... FAILED !\n");
    }

    // update config
    printf("PFBNRomList::build: updating config again...\n");
    ui->getConfig()->load();

#ifndef __VITA__ // TODO: verify why this make release build crashing in vita
    float time_spent = ui->getElapsedTime().asSeconds() - time_start;
    printf("PFBNRomList::build(): list built in %f\n", time_spent);
#endif
}
