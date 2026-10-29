//
// Created by cpasjuste on 19/10/16.
//

#include <vector>
#include <string>
#include <algorithm>
#include "pemu.h"

RomList::RomList(UiMain *_ui, const std::string &emuVersion, const std::vector<std::string> &_filters) {
    printf("RomList()\n");
    ui = _ui;
    filters = _filters;

    // UI
    rect = new C2DRectangle(
            Vector2f(ui->getSize().x - 8, ui->getSize().y - 8));
    ui->getSkin()->loadRectangleShape(rect, {"MAIN"});

    auto *title = new RectangleShape({16, 16});
    if (ui->getSkin()->loadRectangleShape(title, {"MAIN", "TITLE"})) {
        title->setOrigin(Origin::Center);
        title->setPosition(Vector2f(rect->getSize().x / 2, rect->getSize().y / 2));
        float scaling = std::min(
                (rect->getSize().x * 0.8f) / title->getSize().x,
                (rect->getSize().y * 0.8f) / title->getSize().y);
        title->setScale(scaling, scaling);
        rect->add(title);
    } else {
        delete (title);
    }

    text = new Text();
    ui->getSkin()->loadText(text, {"ROM_LIST", "TEXT"});
    text->setOrigin(Origin::BottomLeft);
    text->setPosition(8, rect->getSize().y - 8);
    rect->add(text);

    auto *version = new Text();
    ui->getSkin()->loadText(version, {"ROM_LIST", "TEXT"});
    version->setOrigin(Origin::BottomRight);
    version->setPosition(rect->getSize().x - 8, rect->getSize().y - 8);
    version->setString(emuVersion);
    rect->add(version);

    ui->add(rect);
    //ui->flip();
    // UI

    gameList = new GameList();
    gameListFav = new GameList();
}

void RomList::build(const ss_api::GameList::GameAddedCb &cb) {
    float time_start = ui->getElapsedTime().asSeconds();
    auto cfg = ui->getConfig();
    p_cb = cb;
    m_count = 0;

    //ui->flip();

    auto romPaths = cfg->getRomPaths();
    for (const auto &p: romPaths) {
        std::string gameListPath = p.path + "gamelist.xml";
        // gameList->append(gameListPath, p.path, false, filters, p.system, false, [this](Game *game) {
		// superxyj modified, reduce scanning time by loading only the rom files exists
        gameList->append(gameListPath, p.path, false, filters, p.system, true, [this](Game *game) {
            if (p_cb) p_cb(game);
            // count only the roms that will actually be kept in the list,
            // so the scan counter matches the listbox count (roms that the
            // core driver does not support, bios zips, etc. are excluded)
            if (isGameSupported(*game)) {
                m_count++;
                if (!(m_count % 10)) {
                    setLoadingText(TEXT_MSG_GAMES_SEARCH, m_count);
                }
            }
        });
        //setLoadingText(TEXT_MSG_GAMES_SEARCH, m_games_available_count, m_games_count);
		setLoadingText(TEXT_MSG_GAMES_SEARCH, m_count);
        printf("RomList::build: %s, games found: %zu / %zu (system: %s (0x%08x))\n",
               gameListPath.c_str(), gameList->getAvailableCount(),
               gameList->games.size(), p.system.name.c_str(), p.system.id);
    }

    // sort games
    printf("RomList::build: sorting games...\n");
    std::sort(gameList->systemList.systems.begin(), gameList->systemList.systems.end(), Api::sortSystemByName);
    std::sort(gameList->editors.begin(), gameList->editors.end(), Api::sortEditorByName);
    std::sort(gameList->developers.begin(), gameList->developers.end(), Api::sortDeveloperByName);
    std::sort(gameList->genres.begin(), gameList->genres.end(), Api::sortGenreByName);
    std::sort(gameList->players.begin(), gameList->players.end(), Api::sortInteger);
    std::sort(gameList->ratings.begin(), gameList->ratings.end(), Api::sortInteger);
    std::sort(gameList->rotations.begin(), gameList->rotations.end(), Api::sortInteger);
    std::sort(gameList->resolutions.begin(), gameList->resolutions.end(), Api::sortByName);
    std::sort(gameList->dates.begin(), gameList->dates.end(), Api::sortByName);

    // add filtering options
    printf("RomList::build: add filtering options...\n");
    auto grp = cfg->getGroup(PEMUConfig::GrpId::UI_FILTERING);
    grp->addOption({"FILTER_SYSTEM", gameList->systemList.getNames(), 0,
                    PEMUConfig::OptId::UI_FILTER_SYSTEM, "", TEXT_MENU_FILTER_SYSTEM})->setFlags(PEMUConfig::Flags::HIDDEN);
	//superxyj added, to remove these infrequently used filters
    //grp->getOption(PEMUConfig::OptId::UI_FILTER_SYSTEM)->setArray(gameList->systemList.getNames(), 0);
    //grp->getOption(PEMUConfig::OptId::UI_FILTER_GENRE)->setArray(gameList->getGenreNames(), 0);
    //grp->getOption(PEMUConfig::OptId::UI_FILTER_DATE)->setArray(gameList->getDates(), 0);
    //grp->getOption(PEMUConfig::OptId::UI_FILTER_EDITOR)->setArray(gameList->getEditorNames(), 0);
    //grp->getOption(PEMUConfig::OptId::UI_FILTER_DEVELOPER)->setArray(gameList->getDeveloperNames(), 0);
    //grp->getOption(PEMUConfig::OptId::UI_FILTER_PLAYERS)->setArray(gameList->getPlayersNames(), 0);
    //grp->getOption(PEMUConfig::OptId::UI_FILTER_RATING)->setArray(gameList->getRatingNames(), 0);

    // custom core hide/show flags
    auto ids = cfg->getCoreHiddenOptionToEnable();
    for (const auto &id: ids) {
        cfg->get(id)->setFlags(0);
    }

    // we need to reload config to update new options we just added
    printf("RomList::build: reloading config...\n");
    cfg->load();

    printf("RomList::build(): list built in %f\n", ui->getElapsedTime().asSeconds() - time_start);

    // =====================================================================
    // 中文显示优化：预渲染字形（precache）
    // 首次进入 rom list 时一次性把所有列表/信息面板会用到的字符按主要
    // 字号预光栅化进字体图集。CJK 字体（中文字体）字形庞大且数量是英文
    // 的几十倍，若留到显示第一帧才逐个加载（每个新字形 = FreeType 光栅化
    // + 纹理上传，图集还会从 128 一路 resize 到 2048，每次全量重传），
    // 几百行文本会造成明显的首帧卡顿/花屏。预热把这些开销全部移进当前
    // 的"正在搜索游戏"加载界面里消化掉，显示时就全走缓存。
    // =====================================================================
    {
        printf("RomList::build: precaching font glyphs...\n");
        std::string allText;
        for (const auto &g : gameList->games) {
            allText += g.name;
            allText += g.editor.name;
            allText += g.developer.name;
            allText += g.genre.name;
        }
        for (const auto &s : gameList->systemList.systems) {
            allText += s.name;
        }
        auto *font = ui->getSkin()->getFont();
        if (font && !allText.empty()) {
            // ROM_LIST > TEXT (rom 行文本，主场景)
            font->precacheText(allText, text->getCharacterSize(), false, 0.f);
            // ROM_INFOS 信息面板与 ROM_LIST 标题的字号都从 skin 配置动态
            // 读取（getText 返回的 size 已含皮肤缩放），避免硬编码与配置
            // 不同步（缺配置时 size 为 0，跳过即可）
            auto infoSize = ui->getSkin()->getText({"ROM_INFOS", "SYSTEM_TEXT"}).size;
            auto titleSize = ui->getSkin()->getText({"ROM_LIST", "SYSTEM_TEXT"}).size;
            if (infoSize > 0) {
                font->precacheText(allText, infoSize, false, 0.f);
            }
            if (titleSize > 0) {
                font->precacheText(allText, titleSize, false, 0.f);
            }
        }
    }

    // remove ui widgets
    delete (rect);
}

void RomList::initFav() {
    // load favorites
    delete (gameListFav);
    gameListFav = new GameList();
    gameListFav->append(ui->getIo()->getDataPath() + "favorites.xml");
    for (auto &g: gameListFav->games) {
        auto game = gameList->findGameByPathAndSystem(g.path, g.system.id);
        if (!game.path.empty()) {
            g.available = true;
            g.romsPath = game.romsPath;
        }
    }

    printf("RomList::initFav: %zu favorites\n", gameListFav->games.size());
}

void RomList::addFav(const Game &game) {
    if (!gameListFav->exist(game.id)) {
        gameListFav->games.emplace_back(game);
        gameListFav->save(ui->getIo()->getDataPath() + "favorites.xml", "mixrbv2", "", "video");
    }
}

void RomList::removeFav(const Game &game) {
    if (gameListFav->remove(game.id)) {
        gameListFav->save(ui->getIo()->getDataPath() + "favorites.xml", "mixrbv2", "", "video");
    }
}

void RomList::setLoadingText(const char *format, ...) {
    char buffer[512];
    va_list arg;
    va_start(arg, format);
    vsnprintf(buffer, 512, format, arg);
    va_end(arg);

    text->setString(buffer);
    ui->flip();
}

RomList::~RomList() {
    printf("~RomList()\n");
    delete (gameList);
    delete (gameListFav);
}
