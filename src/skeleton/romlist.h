//
// Created by cpasjuste on 19/10/16.
//

#ifndef C2DUI_ROMLIST_H
#define C2DUI_ROMLIST_H

#include <vector>

namespace pemu {
    class UiMain;

    class RomList {
    public:
        RomList(UiMain *ui, const std::string &emuVersion, const std::vector<std::string> &filters);

        virtual ~RomList();

        virtual void build(const ss_api::GameList::GameAddedCb &cb = nullptr);

        // core hook: tells whether the game will actually be kept in the
        // rom list (driver support, bios zips, ...). The scan counter only
        // counts supported games so it matches the listbox count.
        // default: keep everything (non pfbneo cores are not affected)
        virtual bool isGameSupported(const ss_api::Game &game) {
            (void) game;
            return true;
        }

        virtual void initFav();

        // force a full rescan on the next build() (used by the "scan roms"
        // main menu item); the previous cache is only replaced after the new
        // scan completes.
        void setForceRescan(bool force) { m_forceRescan = force; }

        void addFav(const ss_api::Game &game);

        void removeFav(const ss_api::Game &game);

        void setLoadingText(const char *format, ...);

        UiMain *ui;
        c2d::RectangleShape *rect;
        c2d::Text *text;
        ss_api::GameList *gameList = nullptr;
        ss_api::GameList *gameListFav = nullptr;
        ss_api::GameList::GameAddedCb p_cb = nullptr;

    protected:
        // cache file lives next to the nro: <dataPath>/cache/romlist_cache.bin
        std::string getCacheDir() const;

        std::string getCachePath() const;

        bool saveCache();

        bool loadCache();

        std::vector<std::string> filters;
        size_t m_count = 0;
        bool m_forceRescan = false;
    };
}

#endif //C2DUI_ROMLIST_H
