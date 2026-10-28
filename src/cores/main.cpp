//
// Created by cpasjuste on 19/09/23.
//

#include "main.h"

#include <cstdio>

using namespace c2d;
using namespace pemu;

PEMUUiMain *pemu_ui;

// ---- CRC32 (IEEE 802.3, polynomial 0xEDB88320) ----
namespace {
    unsigned int crc32Table[256];
    bool crc32TableReady = false;

    void crc32InitTable() {
        if (crc32TableReady) {
            return;
        }
        for (unsigned int i = 0; i < 256; i++) {
            unsigned int c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            crc32Table[i] = c;
        }
        crc32TableReady = true;
    }

    unsigned int crc32File(const char *path) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            return 0;
        }
        crc32InitTable();
        unsigned int crc = 0xFFFFFFFFu;
        unsigned char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            for (size_t i = 0; i < n; i++) {
                crc = crc32Table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
            }
        }
        fclose(f);
        return crc ^ 0xFFFFFFFFu;
    }
}

// donate.png integrity flag: defined in the skeleton (ui_menu.cpp, defaults to
// true so non-pfbneo cores are unaffected), checked once at startup below and
// read by UiMenu::load (menu shows only "退出") and PFBAUiEmu::load (roms
// locked). any tampering (CRC mismatch or missing file) is treated as
// modified.
static void checkDonateIntegrity(C2DIo *io) {
    if (!io) {
        g_donateValid = false;
        return;
    }
    const std::string path = io->getRomFsPath() + "donate/donate.png";
    const unsigned int crc = crc32File(path.c_str());
    g_donateValid = (crc == 0x74b88c25u);
    printf("pfbneo: donate.png CRC32 = 0x%08X, %s\n", crc,
           g_donateValid ? "OK" : "TAMPERED (features locked)");
}

int main(int argc, char **argv) {
    // command line game info
    Game game;

    // custom io
    const auto io = new PEMUIo();

    // create main ui/renderer
    // NOTE: size must stay {0, 0} on non-switch platforms (PS4, PS5, Vita, Linux, Windows...).
    // libcross2d's SDL2Renderer only sets SDL_WINDOW_FULLSCREEN_DESKTOP when the requested
    // size is <= 0 (see SDL2Renderer::SDL2Renderer in libcross2d/source/platforms/sdl2/sdl2_renderer.cpp).
    // Passing a fixed size like {1280, 720} here creates a small, non-fullscreen SDL window,
    // which on PS4 (no windowing system / no desktop compositor) never actually gets
    // presented to the TV output -> black screen, even though audio keeps working fine
    // since it's an independent subsystem. Switch ignores this parameter entirely and
    // forces its own {1280, 720} internally (see UiMain's __SWITCH__ constructor), so it's
    // safe to leave this at {0, 0} for every platform.
#if defined(__LINUX__) || defined(__WINDOWS__) || defined(__SWITCH__)
    pemu_ui = new PEMUUiMain(Vector2f{1280, 720});
#else
    pemu_ui = new PEMUUiMain(Vector2f{0, 0});
#endif
    pemu_ui->setIo(io);

    // load configuration
    constexpr int version = (__PEMU_VERSION_MAJOR__ * 100) + __PEMU_VERSION_MINOR__;
    const auto cfg = new PEMUConfig(pemu_ui, version);
    pemu_ui->setConfig(cfg);

    // load skin configuration
    const auto skin = new PEMUSkin(pemu_ui);
    pemu_ui->setSkin(skin);

    // donate.png integrity check (locks roms and trims the +/- menus on tamper)
    checkDonateIntegrity(io);

    // parse command line
    if (argc > 1) {
        if (io->exist(argv[1])) {
            game.path = Utility::baseName(argv[1]);
            game.name = Utility::removeExt(game.path);
            game.romsPath = Utility::remove(argv[1], game.path);
        } else {
            printf("main: file provided as console argument does not exist (%s)\n", argv[1]);
            delete (skin);
            delete (cfg);
            delete (pemu_ui);
            return 1;
        }
    }

    // ui
    const auto romList = new PEMURomList(pemu_ui, cfg->getCoreVersion(), cfg->getCoreSupportedExt());
    if (game.path.empty()) {
        romList->build();
        romList->initFav();
    } else {
        delete (romList->rect);
    }
    const auto uiRomList = new PEMUUiRomList(pemu_ui, romList, pemu_ui->getSize());
    const auto uiMenu = new PEMUUiMenu(pemu_ui);
    const auto uiEmu = new PEMUUiEmu(pemu_ui);
    const auto uiState = new PEMUUiMenuState(pemu_ui);
    pemu_ui->init(uiRomList, uiMenu, uiEmu, uiState);

    // pfbneo cheat dialog: attach to the ui tree right after init, exactly
    // like the other widgets (UiMenuState etc). attaching at runtime (from an
    // input callback) can be skipped by the c2d render traversal, which is why
    // the dialog was never drawn.
    uiEmu->attachToUi();

    // donate dialog: attached to the TOP of the ui tree (screen coords), same
    // reason - must happen after init, not from an input callback.
    uiMenu->attachDonateDialog();

    // load specified game from command line if requested
    if (!game.path.empty()) {
        uiRomList->setVisibility(Visibility::Hidden);
        uiRomList->setGames({game});
        cfg->loadGame(game);
        uiEmu->setExitOnStop(true);
        uiEmu->load(game);
    }

    while (!pemu_ui->done) {
        pemu_ui->flip();
    }

    delete (skin);
    delete (cfg);
    delete (pemu_ui);

#ifdef  __PS4__
    sceSystemServiceLoadExec((char *) "exit", nullptr);
    while (true) {
    }
#endif

    return 0;
}
