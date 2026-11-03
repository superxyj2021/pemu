//
// Created by cpasjuste on 01/06/18.
//

#include "burner.h"
#include "burnint.h"

#include "skeleton/pemu.h"
#include "skeleton/buffer.h"
#include "pfbneo_ui_emu.h"
#include "pfbneo_ui_video.h"
#include "pfbneo_utility.h"
#include "pfbneo_keybind.h"
#include "pfbneo_ui_cheat.h"
#include "pfbneo_ui_key.h"
#include "pfbneo_ui_dip.h"
#include "pfbneo_ui_memcard.h"
#include "pfbneo_ui_ips.h"
#include "pfbneo_ui_movelist.h"
#include "fbneo/pfbneo_cheat.h"
#include "fbneo/pfbneo_dip.h"
#include "fbneo/pfbneo_memcard.h"
#include "fbneo/pfbneo_ips.h"
#include "fbneo/pfbneo_movelist.h"
#include "fbneo/pfbneo_bezel.h"
#include "retro_input_wrapper.h"

#include <chrono>

using namespace c2d;
using namespace pemu;

int nVidFullscreen = 0;
INT32 bVidUseHardwareGamma = 1;

UINT32 (__cdecl *VidHighCol)(INT32 r, INT32 g, INT32 b, INT32 i);

INT32 VidRecalcPal() { return BurnRecalcPal(); }

extern const char *GetRomsetError();
extern void ClearRomsetError();

#ifdef __PFBA_ARM__
extern int nSekCpuCore;

static bool isHardware(int hardware, int type) {
    return (((hardware | HARDWARE_PREFIX_CARTRIDGE) ^ HARDWARE_PREFIX_CARTRIDGE)
            & 0xff000000) == (unsigned int) type;
}

#endif

static UINT32 myHighCol16(int r, int g, int b, int /* i */) {
    UINT32 t;
    t = (r << 8) & 0xf800;
    t |= (g << 3) & 0x07e0;
    t |= (b >> 3) & 0x001f;
    return t;
}

static UiMain *uiInstance;

PFBAUiEmu::PFBAUiEmu(UiMain *ui) : UiEmu(ui) {
    printf("PFBAUiEmu()\n");
    uiInstance = ui;

    // pfbneo cheat dialog: attached as a child of the emu object itself, so
    // it is drawn AFTER the emulated frame (on top of the game screen) and
    // receives input through the emu subtree. The emu is hidden when no game
    // is running, which automatically hides the dialog as well.
    // pfbneo cheat dialog: created here, attached to the ui tree (as the
    // LAST object, on top of everything) on first open() - see
    // PfbneCheatDialog::open()
    cheatDialog = new PfbneCheatDialog(ui);

    // pfbneo key settings dialog (same attach strategy)
    keyDialog = new PfbneKeyDialog(ui);

    // pfbneo DIP switch dialog (same attach strategy)
    dipDialog = new PfbneDipDialog(ui);

    // pfbneo memory card dialog (same attach strategy)
    memcardDialog = new PfbneMemcardDialog(ui);

    // pfbneo IPS patch dialog (same attach strategy)
    ipsDialog = new PfbneIpsDialog(ui);

    // pfbneo move list (出招表) dialog (same attach strategy)
    movelistDialog = new PfbneMovelistDialog(ui);
}

bool PFBAUiEmu::isExtraDialogOpen() {
    return (cheatDialog && cheatDialog->isVisible()) || (keyDialog && keyDialog->isVisible())
           || (dipDialog && dipDialog->isVisible()) || (memcardDialog && memcardDialog->isVisible())
           || (ipsDialog && ipsDialog->isVisible()) || (movelistDialog && movelistDialog->isVisible());
}

bool PFBAUiEmu::hasExtraFeature(const std::string &name) {
    if (name == TEXT_MENU_DIP) {
        // only offer the DIP menu when the selected game's driver actually
        // defines DIP switches (checked after loadGame built the switch list)
        return pfbneo_dip::getRowCount() > 0;
    }
    if (name == TEXT_MENU_MEMCARD) {
        // only offer the memory card menu when the selected game actually
        // supports the external card (NEOGEO cart games / PGM2 card versions)
        return pfbneo_memcard::hasMemcard();
    }
    if (name == TEXT_MENU_IPS) {
        // only offer the IPS patch menu when the selected game actually has
        // .dat patch files under <data path>/ips/<driver>/
        return pfbneo_ips::hasIps();
    }
    if (name == TEXT_MENU_MOVELIST) {
        // only offer the move list menu when command.dat has an entry for the
        // selected game (under <data path>/commands/command.dat)
        return pfbneo_movelist::hasMovelist();
    }
    if (name == TEXT_CHEATS) {
        // only offer the cheat menu when the running game actually has cheats
        // (loaded from an ini file or a mame .dat database at game start)
        return pfbneo_cheat::getCheatCount() > 0;
    }
    return false;
}

void PFBAUiEmu::requestReset() {
    m_resetRequested = true;
}

void PFBAUiEmu::markSettingsAction(int action) {
    // the higher action wins: a reload (2) also covers any pending reset (1)
    if (action > m_settingsAction) {
        m_settingsAction = action;
    }
}

int PFBAUiEmu::takeSettingsAction() {
    int action = m_settingsAction;
    m_settingsAction = 0;
    return action;
}

void PFBAUiEmu::reload() {
    // called when the in-game menu closes while a dialog changed a setting
    // that only takes effect at game start: stop the running game and load it
    // again, which re-runs DrvInit (IPS rom patches) and onGameStart (DIP
    // switches + memory card insertion).
    if (pMain->getUiMenu()) {
        pMain->getUiMenu()->setVisibility(Visibility::Hidden, true);
    }
    if (currentGame.path.empty()) {
        resume();
        return;
    }
    Game game = currentGame;
    stop();
    load(game);
}

void PFBAUiEmu::attachToUi() {
    if (cheatDialog && !m_dialogAttached) {
        pMain->add(cheatDialog);
        pMain->add(keyDialog);
        pMain->add(dipDialog);
        pMain->add(memcardDialog);
        pMain->add(ipsDialog);
        pMain->add(movelistDialog);
        m_dialogAttached = true;
    }
}

bool PFBAUiEmu::onMenuExtra(const std::string &name) {
    if (name == TEXT_CHEATS) {
        // rom menu -> 金手指: open the cheat dialog (game is running)
        if (cheatDialog) {
            cheatDialog->open();
        }
        return true;
    }
    if (name == TEXT_MENU_KEYS) {
        // rom menu / in-game menu -> 按键设置: open the key dialog
        if (keyDialog) {
            keyDialog->open();
        }
        return true;
    }
    if (name == TEXT_MENU_DIP) {
        // rom menu / in-game menu -> DIP 开关: open the DIP dialog
        if (dipDialog) {
            dipDialog->open();
        }
        return true;
    }
    if (name == TEXT_MENU_MEMCARD) {
        // rom menu / in-game menu -> 记忆卡: open the memory card dialog
        if (memcardDialog) {
            memcardDialog->open();
        }
        return true;
    }
    if (name == TEXT_MENU_IPS) {
        // rom menu / in-game menu -> IPS补丁: open the IPS patch dialog
        if (ipsDialog) {
            ipsDialog->open();
        }
        return true;
    }
    if (name == TEXT_MENU_MOVELIST) {
        // rom menu / in-game menu -> 出招表: open the move list dialog
        if (movelistDialog) {
            movelistDialog->open();
        }
        return true;
    }
    if (name == TEXT_MENU_RESET) {
        // in-game menu -> 重启游戏: latch a reset request; the pulse is sent
        // in onUpdate() right after InputMake() so the driver frame sees it
        // (same timing as the Select-button long-press reset)
        m_resetRequested = true;
        // the reset also covers any dialog change that is still pending
        // (DIP switches latch at reset), so it must not trigger a restart
        // when the menu reopens later
        m_settingsAction = 0;
        resume();
        return true;
    }
    return false;
}

#ifdef __PFBA_ARM__

int PFBAUiEmu::getSekCpuCore() {
    int sekCpuCore = 0; // SEK_CORE_C68K: USE CYCLONE ARM ASM M68K CORE

    std::vector<std::string> zipList;
    int hardware = BurnDrvGetHardwareCode();

    // UniBIOS entries are the first 4 of the NEOBIOS array (indices 0..3)
    int bios = pMain->getConfig()->get(PEMUConfig::OptId::EMU_NEOBIOS, true)->getArrayIndex();
    if (isHardware(hardware, HARDWARE_PREFIX_SNK) && bios <= 3) {
        sekCpuCore = 1; // SEK_CORE_M68K: USE C M68K CORE
        pMain->getUiMessageBox()->show(
                TEXT_MSG_TITTLE_WARNING, TEXT_MSG_UNIBIOS_DONT_SUPPORT_M68K_ASM_CORE, TEXT_BUTTON_OK);
    }

    if (isHardware(hardware, HARDWARE_PREFIX_SEGA_MEGADRIVE)) {
        sekCpuCore = 1; // SEK_CORE_M68K: USE C M68K CORE
    } else if (isHardware(hardware, HARDWARE_PREFIX_SEGA)) {
        if (hardware & HARDWARE_SEGA_FD1089A_ENC
            || hardware & HARDWARE_SEGA_FD1089B_ENC
            || hardware & HARDWARE_SEGA_MC8123_ENC
            || hardware & HARDWARE_SEGA_FD1094_ENC
            || hardware & HARDWARE_SEGA_FD1094_ENC_CPU2) {
            sekCpuCore = 1; // SEK_CORE_M68K: USE C M68K CORE
            pMain->getUiMessageBox()->show(
                    TEXT_MSG_TITTLE_WARNING, TEXT_MSG_ROM_CRYPTED, TEXT_BUTTON_OK);
        }
    } else if (isHardware(hardware, HARDWARE_PREFIX_TOAPLAN)) {
        zipList.emplace_back("batrider");
        zipList.emplace_back("bbakraid");
        zipList.emplace_back("bgaregga");
    } else if (isHardware(hardware, HARDWARE_PREFIX_SNK)) {
        zipList.emplace_back("kof97");
        zipList.emplace_back("kof98");
        zipList.emplace_back("kof99");
        zipList.emplace_back("kof2000");
        zipList.emplace_back("kof2001");
        zipList.emplace_back("kof2002");
        zipList.emplace_back("kf2k3pcb");
        //zipList.push_back("kof2003"); // WORKS
    }

    std::string zip = BurnDrvGetTextA(DRV_NAME);
    for (unsigned int i = 0; i < zipList.size(); i++) {
        if (zipList[i].compare(0, zip.length(), zip) == 0) {
            pMain->getUiStatusBox()->show(TEXT_MSG_NOT_SUPPORT_M68K_ASM_CODE);
            sekCpuCore = 1; // SEK_CORE_M68K: USE C M68K CORE
            break;
        }
    }

    zipList.clear();

    return sekCpuCore;
}

#endif

int PFBAUiEmu::load(const ss_api::Game &game) {
    // clear any romset error from a previous failed load
    ClearRomsetError();
    // a fresh game load has no pending "restart action" state from an
    // earlier dialog session (it was consumed on the previous menu close or
    // belongs to the previous game)
    m_settingsAction = 0;

    // embedded-image probe: re-check every time a rom is about to be opened.
    // digest split in two halves so the 64-bit reference is never visible as
    // a single literal; a mismatch is treated exactly like a failed startup
    // check (roms locked out, menus trimmed).
    unsigned long long _r = x7fq(buffer_img, (unsigned long long) BUFFER_IMG_SIZE);
    if (!(((unsigned int) (_r >> 32) == 0x6DCB26ADu) &&
          ((unsigned int) _r == 0xED485F60u))) {
        g_qq7f = false;
        printf("PFBAUiEmu::load: embedded image digest mismatch, rom locked\n");
    }

    // integrity gate: startup/embedded-image probe failed -> roms are locked out
    if (!g_qq7f) {
        printf("PFBAUiEmu::load: integrity gate failed, rom locked\n");
        pMain->getUiMessageBox()->show(TEXT_MSG_TITTLE_ERROR, TEXT_MSG_DONATE_LOCKED, TEXT_BUTTON_OK);
        return -1;
    }

    currentGame = game;

    // load the per-rom config now (turbo settings + emulation/gamepad options).
    // this must happen before DrvInit so that saved values (e.g. turbo) are
    // applied from the very first frame and game specific options are used.
    pMain->getConfig()->loadGame(game);

    PFBNEOUtility::setDriverActive(game);
    if (nBurnDrvActive >= nBurnDrvCount) {
        printf("PFBAUiEmu::load: driver not found\n");
        pMain->getUiProgressBox()->setVisibility(Visibility::Hidden);
        pMain->getUiMessageBox()->show(TEXT_MSG_TITTLE_ERROR, TEXT_MSG_DRIVER_NOT_FOUND, TEXT_BUTTON_OK);
        return -1;
    }

    // show the loading screen: use the SAME (chinese) display name used in the
    // rom list (getGameInfo -> GetGameDisplayName). the glyphs of the title and
    // message are pre-warmed into the font atlas BEFORE the first present, so
    // the first frame is never drawn while the atlas is still being filled
    // (that was the cause of the garbled/flickering title). the progress bar is
    // then kept moving by the throttled flip() in ProgressUpdateBurner.
    {
        auto gameInfo = PFBNEOUtility::getGameInfo(currentGame,
                                                   PFBNEOUtility::getChineseTxtPath(pMain->getIo()));
        auto *progressBox = pMain->getUiProgressBox();
        progressBox->setTitle(gameInfo.name.empty() ? currentGame.name : gameInfo.name);
        progressBox->setMessage(TEXT_MSG_PLEASE_WAIT);
        progressBox->setProgress(0);
        progressBox->setVisibility(Visibility::Visible);
        progressBox->setLayer(1000);
        // pre-warm: force the title + message glyphs into the font atlas now
        // (getSize() -> ensureGeometryUpdate() -> Font::getGlyph) so the first
        // present below is clean.
        progressBox->getTitleText()->getSize();
        progressBox->getMessageText()->getSize();
        pMain->flip();
    }

#ifdef __PFBA_ARM__
    nSekCpuCore = getSekCpuCore();
    printf("nSekCpuCore: %s\n", nSekCpuCore > 0 ? "M68K" : "C68K (ASM)");
#endif

    #if defined(__PS4__) || defined(ANDROID) || defined(__SWITCH__)
    // force 48000hz audio output (matches the switch/ps4/android audio hw,
    // avoids an extra resample step from the 44100 default). applied here as
    // well: the config can be reloaded from a saved file between startup and
    // the first game load, which would otherwise bring back an old 44100
    pMain->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_FREQ, true)->setArrayIndex(4);
#endif
    int audio_freq = pMain->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_FREQ, true)->getInteger();
    nInterpolation = pMain->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_INTERPOLATION, true)->getInteger();
    nFMInterpolation = pMain->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_FMINTERPOLATION, true)->getInteger();
    bForce60Hz = pMain->getConfig()->get(PEMUConfig::OptId::EMU_FORCE_60HZ, true)->getInteger();
    if (bForce60Hz) {
        nBurnFPS = 6000;
    }

    ///////////////
    // FBA DRIVER
    ///////////////
    EnableHiscores = 1;

    printf("PFBAUiEmu::load: initialize driver...\n");
    printf("PFBAUiEmu::load: game: %s (zip: %s), driver: %s (%s)\n",
           currentGame.name.c_str(), currentGame.path.c_str(),
           BurnDrvGetTextA(DRV_FULLNAME), BurnDrvGetTextA(DRV_NAME));
    // some drivers require audio buffer to be allocated for DrvInit, add a "dummy" one for now...
    auto *aud = new Audio(audio_freq);
    nBurnSoundRate = aud->getSampleRate();
    nBurnSoundLen = aud->getSamples();
    pBurnSoundOut = (INT16 *) malloc(aud->getSamplesSize());
    // pfbneo IPS: apply the enabled patches BEFORE the core loads the roms
    // (DrvInit -> BurnLoadRom -> IpsApplyPatches). the per-rom IPS options
    // were created by onGameConfigCreate and already restored by load().
    pfbneo_ips::applyEnabled();
    if (DrvInit((int) nBurnDrvActive, false) != 0) {
        printf("\nPFBAUiEmu::load: driver initialisation failed for %s (%s), driver: %s (%s)\n",
               currentGame.name.c_str(), currentGame.path.c_str(),
               BurnDrvGetTextA(DRV_FULLNAME), BurnDrvGetTextA(DRV_NAME));
        // dump the expected roms of the active driver (name/size/crc) so a
        // missing or wrong romset can be spotted right away
        struct BurnRomInfo ri;
        for (int i = 0; BurnDrvGetRomInfo(&ri, i) == 0; i++) {
            if (ri.nType != 0) {
                char *romName = nullptr;
                BurnDrvGetRomName(&romName, i, 0);
                printf("  rom[%02d]: %-16s len=%d crc=0x%08x type=0x%02x\n",
                       i, romName ? romName : "(null)", ri.nLen, ri.nCrc, ri.nType);
            }
        }
        delete (aud);
        pMain->getUiProgressBox()->setVisibility(Visibility::Hidden);
        // show the specific romset problem if the romset check caught one
        // (missing/wrong roms), otherwise the generic driver init failure
        const char *romsetErr = GetRomsetError();
        if (romsetErr && romsetErr[0]) {
            std::string msg = TEXT_MSG_ROM_SET_BAD;
            msg += romsetErr;
            pMain->getUiMessageBox()->show(TEXT_MSG_TITTLE_ERROR, msg, TEXT_BUTTON_OK);
        } else {
            pMain->getUiMessageBox()->show(TEXT_MSG_TITTLE_ERROR, TEXT_MSG_DRIVER_INIT_FAILED, TEXT_BUTTON_OK);
        }
        stop();
        return -1;
    }
    // driver initialised: force the loading bar to 100% (the core drivers
    // only report progress up to the last internal stage, so without this the
    // bar can stall below the end and never reach it; same pattern as
    // pgba/pnes/psnes which call setProgress(1) after a successful load)
    pMain->getUiProgressBox()->setProgress(1);
    // present the 100% frame immediately: the bar data was just updated but
    // without a flip here the screen keeps showing the last mid-load frame
    // (stuck at ~90%) until UiEmu::load hides the box a few lines later.
    pMain->flip();
    // unregister the core progress callbacks: they are only needed during
    // DrvInit. leaving them active lets any later BurnUpdateProgress() /
    // BurnSetProgressRange() call touch the UI (and flip) while the game is
    // already running; the next load re-registers them in ProgressCreate().
    BurnExtProgressUpdateCallback = NULL;
    BurnExtProgressRangeCallback = NULL;
    delete (aud);
    free(pBurnSoundOut);
    nFramesEmulated = 0;
    nFramesRendered = 0;
    nCurrentFrame = 0;
    m_frameTimeAvg = 0.f;
    m_autoSlowFrames = 0;
    m_autoFastFrames = 0;
    m_autoFrameskip = 0;
    ///////////////
    // FBA DRIVER
    ///////////////

    ///////////
    // AUDIO
    //////////
    addAudio(audio_freq, Audio::toSamples(audio_freq, (float) nBurnFPS / 100.0f));
    if (audio->isAvailable()) {
        nBurnSoundRate = audio->getSampleRate();
        nBurnSoundLen = audio->getSamples();
        pBurnSoundOut = (INT16 *) malloc(audio->getSamplesSize());
    }
    audio_sync = !bForce60Hz;
    targetFps = (float) nBurnFPS / 100.0f;
    printf("PFBAUiEmu::load: FORCE_60HZ: %i, AUDIO_SYNC: %i, FPS: %f\n", bForce60Hz, audio_sync, targetFps);
    ///////////
    // AUDIO
    //////////

    //////////
    // VIDEO
    //////////
    Vector2i size, aspect;
    BurnDrvGetFullSize(&size.x, &size.y);
    BurnDrvGetAspect(&aspect.x, &aspect.y);
    nBurnBpp = 2;
    BurnHighCol = myHighCol16;
    BurnRecalcPal();
    // video may already be initialized from fbneo driver (Reinitialise)
    if (!video) {
        auto v = new PFBAVideo(pMain, &pBurnDraw, &nBurnPitch, size, aspect);
        addVideo(v);
        printf("PFBAUiEmu::load: size: %i x %i, aspect: %i x %i, pitch: %i\n",
               size.x, size.y, aspect.x, aspect.y, nBurnPitch);
    } else {
        printf("PFBAUiEmu::load: video already initialized, skipped\n");
    }
    //////////
    // VIDEO
    //////////

    // pfbneo keybind: resolve the per-rom key bindings against the running
    // game's input mapping + log game start
    pfbneo_keybind::onGameStart();

    // pfbneo DIP: apply the saved DIP switch settings to the running game
    // (called after InputInit mapped GameInp and ran InpDIPSWResetDIPs)
    pfbneo_dip::onGameStart();

    // pfbneo memcard: insert the saved card selections into the running core
    pfbneo_memcard::onGameStart();

    // pfbneo cheat: parse cheats/<rom>.ini and rebuild the core list.
    // runtime-only: nothing is persisted, all cheats start inactive.
    pfbneo_cheat::load(game, pMain->getIo());

    // pfbneo bezel: load the game's bezel background (per the menu mode) and
    // add it BEHIND the video texture; updateScaling() then fits the game
    // inside the bezel's hollow viewport
    applyBezel();

#ifdef __SWITCH__
    // apply the emulation performance mode (0 = power save, 1 = full speed).
    // per-rom value takes priority over the global one (see PEMUConfig::loadGame).
    // the UI always keeps stock clocks, we only boost once a game is running.
    int perfMode = pMain->getConfig()->get(PEMUConfig::OptId::EMU_PERF_MODE, true)->getArrayIndex();
    PEMUClock::setGameMode(perfMode);
#endif

    return UiEmu::load(game);
}

// need for some games
void Reinitialise(void) {
    Vector2i size, aspect;
    BurnDrvGetFullSize(&size.x, &size.y);
    BurnDrvGetAspect(&aspect.x, &aspect.y);
    auto v = new PFBAVideo(uiInstance, &pBurnDraw, &nBurnPitch, size, aspect);
    uiInstance->getUiEmu()->addVideo(v);
    printf("PFBAUiEmu::Reinitialise: size: %i x %i, aspect: %i x %i\n",
           size.x, size.y, aspect.x, aspect.y);
}

// need for some games
void ReinitialiseVideo()
{
	Reinitialise();
}

void PFBAUiEmu::applyBezel() {
    // remove any previously loaded bezel first
    if (pfbneo_bezel::isActive()) {
        printf("pfbneo_bezel: applyBezel: removing previous\n");
        remove(pfbneo_bezel::getSprite());
    }
    pfbneo_bezel::unload();

    // menu mode: 0 = 开, 1 = 关
    auto *bezelOpt = pMain->getConfig()->get(PEMUConfig::OptId::EMU_BEZEL, true);
    int mode = bezelOpt ? bezelOpt->getArrayIndex() : 0;
    printf("pfbneo_bezel: applyBezel: mode = %i (%s)\n", mode,
           bezelOpt ? bezelOpt->getString().c_str() : "?");
    if (mode == 1) {
        printf("pfbneo_bezel: applyBezel: disabled\n");
        return;
    }

    // bezel on -> force fit-screen scaling (the bezel fills the screen and
    // the game is contained inside its hollow viewport)
    auto *scalingOpt = pMain->getConfig()->get(PEMUConfig::OptId::EMU_SCALING, true);
    if (scalingOpt && scalingOpt->getString() != TEXT_SCALING_FIT) {
        scalingOpt->setArrayIndex(TEXT_SCALING_FIT);
        printf("pfbneo_bezel: applyBezel: forced scaling to %s\n", TEXT_SCALING_FIT);
    }

    const char *drv = BurnDrvGetTextA(DRV_NAME);
    const char *par = BurnDrvGetTextA(DRV_PARENT);
    if (!drv || !drv[0]) {
        printf("pfbneo_bezel: applyBezel: no driver name yet\n");
        return;
    }
    unsigned int hw = BurnDrvGetHardwareCode();
    bool vertical = (BurnDrvGetFlags() & BDF_ORIENTATION_VERTICAL) != 0;
    if (!pfbneo_bezel::load(drv, par ? par : "", hw, vertical)) {
        printf("pfbneo_bezel: applyBezel: load failed\n");
        return;
    }

    c2d::Texture *sprite = pfbneo_bezel::getSprite();
    if (sprite) {
        // draw BEHIND the video texture (lower layer sorts first)
        sprite->setLayer(-1);
        add(sprite);
        if (video) {
            video->updateScaling();
        }
        printf("pfbneo_bezel: applyBezel: sprite added (layer -1), size %ix%i\n",
               sprite->getTextureSize().x, sprite->getTextureSize().y);
    } else {
        printf("pfbneo_bezel: applyBezel: sprite is null\n");
    }
}

void PFBAUiEmu::onBezelChanged() {
    applyBezel();
}

void PFBAUiEmu::stop() {
    // pfbneo keybind: clear runtime state
    pfbneo_keybind::onGameStop();
    // pfbneo DIP: clear runtime state
    pfbneo_dip::onGameStop();
    // pfbneo memcard: clear runtime state
    pfbneo_memcard::onGameStop();
    // pfbneo IPS: clear runtime state + reset the core patch globals
    pfbneo_ips::onGameStop();
    // pfbneo move list: clear runtime state
    pfbneo_movelist::onGameStop();
    // pfbneo cheat: clear runtime state (the core's CheatExit() already
    // freed the pCheatInfo list during DrvExit); hide the dialog if open
    if (cheatDialog) {
        cheatDialog->setVisibility(Visibility::Hidden, true);
    }
    if (keyDialog) {
        keyDialog->setVisibility(Visibility::Hidden, true);
    }
    if (dipDialog) {
        dipDialog->setVisibility(Visibility::Hidden, true);
    }
    if (memcardDialog) {
        memcardDialog->setVisibility(Visibility::Hidden, true);
    }
    if (ipsDialog) {
        ipsDialog->setVisibility(Visibility::Hidden, true);
    }
    if (movelistDialog) {
        movelistDialog->setVisibility(Visibility::Hidden, true);
    }
    // pfbneo bezel: remove the background sprite from the tree and free it
    if (pfbneo_bezel::isActive()) {
        remove(pfbneo_bezel::getSprite());
    }
    pfbneo_bezel::unload();
    m_resetRequested = false;
    m_settingsAction = 0;
    pfbneo_cheat::reset();
    DrvExit();
    if (pBurnSoundOut) {
        free(pBurnSoundOut);
        pBurnSoundOut = nullptr;
    }
    UiEmu::stop();
}

bool PFBAUiEmu::onInput(c2d::Input::Player *players) {
    if (pMain->getUiMenu()->isVisible() || pMain->getUiStateMenu()->isVisible()) {
        pMain->getInput()->setRotation(Input::Rotation::R0, Input::Rotation::R0);
        return UiEmu::onInput(players);
    }

    // pfbneo cheat/key dialogs: while one is open, let the dialog (added
    // after the emu in the ui tree) handle all input instead of the game
    if ((cheatDialog && cheatDialog->isVisible()) || (keyDialog && keyDialog->isVisible())
        || (dipDialog && dipDialog->isVisible()) || (memcardDialog && memcardDialog->isVisible())
        || (ipsDialog && ipsDialog->isVisible()) || (movelistDialog && movelistDialog->isVisible())) {
        return false;
    }

    // rotation config:
    // 0 > "OFF"
    // 1 > "ON"
    // 2 > "FLIP"
    // 3 > "CAB" (vita/switch)
    int rotation = getUi()->getConfig()->get(PEMUConfig::OptId::EMU_ROTATION, true)->getArrayIndex();
    if (BurnDrvGetFlags() & BDF_ORIENTATION_VERTICAL) {
        if (rotation == 0) {
            pMain->getInput()->setRotation(Input::Rotation::R90, Input::Rotation::R0);
        } else if (rotation == 1) {
            pMain->getInput()->setRotation(Input::Rotation::R0, Input::Rotation::R0);
        } else if (rotation == 2) {
            pMain->getInput()->setRotation(Input::Rotation::R270, Input::Rotation::R0);
        } else {
            pMain->getInput()->setRotation(Input::Rotation::R270, Input::Rotation::R270);
        }
    }

    return UiEmu::onInput(players);
}

void PFBAUiEmu::onUpdate() {
    if (isPaused()) {
        return;
    }

    // update fbneo inputs
    InputMake(true);

    // handle diagnostic and reset switch
    unsigned int buttons = pMain->getInput()->getButtons();
    if (buttons & Input::Button::Select) {
        if (clock.getElapsedTime().asSeconds() > 2) {
            if (pgi_reset) {
                pMain->getUiStatusBox()->show(TEXT_MSG_PRESS_BUTTON2_FOR_DIAG_MENU);
                pgi_reset->Input.nVal = 1;
                *(pgi_reset->Input.pVal) = pgi_reset->Input.nVal;
            }
            nCurrentFrame = 0;
            nFramesEmulated = 0;
            clock.restart();
        }
    } else if (buttons & Input::Button::Start) {
        if (clock.getElapsedTime().asSeconds() > 2) {
            if (pgi_diag) {
                pMain->getUiStatusBox()->show(TEXT_MSG_PRESS_COIN_BUTTON_TO_RESET);
                pgi_diag->Input.nVal = 1;
                *(pgi_diag->Input.pVal) = pgi_diag->Input.nVal;
            }
            clock.restart();
        }
    } else {
        clock.restart();
    }

    // "reset game" requested from the rom menu: send the same reset-switch
    // pulse as the Select-button long-press above (set AFTER InputMake so the
    // driver frame reads it, and clear the frame counters)
    if (m_resetRequested) {
        m_resetRequested = false;
        if (pgi_reset) {
            pgi_reset->Input.nVal = 1;
            *(pgi_reset->Input.pVal) = pgi_reset->Input.nVal;
        }
        nCurrentFrame = 0;
        nFramesEmulated = 0;
    }

    // update fbneo video buffer and audio
#if defined(__VITA__) || defined(__SWITCH__)
    // frameskip mode (array index): 0 = OFF, 1 = 自动 (auto), 2..10 = manual skip 1..9.
    // read live every frame: the per-rom config is deleted/recreated whenever
    // the in-game menu opens (UiMenu::load -> loadGame), so a cached option
    // pointer would dangle (UAF) and the value would freeze at the pre-menu state.
    auto *fsOpt = pMain->getConfig()->get(PEMUConfig::OptId::EMU_FRAMESKIP, true);
    const int frameskipMode = fsOpt ? fsOpt->getArrayIndex() : 0;
    const int manualSkip = frameskipMode >= 2 ? frameskipMode - 1 : 0;
#else
    const int manualSkip = 0;
#endif
#if defined(__SWITCH__)
    const bool autoSkipEnabled = frameskipMode == 1;
#else
    const bool autoSkipEnabled = false;
#endif
    // effective skip for this frame: manual + the auto level learned so far.
    // must be applied BEFORE the render decision below, otherwise auto skipping
    // never affects which frames are rendered.
    int skip = manualSkip;
#if defined(__SWITCH__)
    if (autoSkipEnabled && m_autoFrameskip > skip) {
        skip = m_autoFrameskip;
    }
#endif

    pBurnDraw = nullptr;
    frameskip++;

    if (frameskip > skip) {
        video->getTexture()->lock(&pBurnDraw, &nBurnPitch);
        nFramesRendered++;
    }

#if defined(__SWITCH__)
    // measure BurnDrvFrame only on rendered frames: a skipped frame is cheap by
    // design (pBurnDraw == nullptr), so averaging it in would make the load look
    // fine while skipping and turn the skip back off (self-oscillation). the
    // render-frame time is independent of the current skip level, so the auto
    // decision stays stable.
    std::chrono::steady_clock::time_point frameStart{};
    const bool measuring = autoSkipEnabled && (frameskip > skip);
    if (measuring) {
        frameStart = std::chrono::steady_clock::now();
    }
#endif
    BurnDrvFrame();
    nCurrentFrame++;
#if defined(__SWITCH__)
    if (measuring) {
        const auto frameEnd = std::chrono::steady_clock::now();
        const float renderFrameTime =
                std::chrono::duration<float>(frameEnd - frameStart).count();
        const float budget = 1.0f / targetFps;
        if (m_frameTimeAvg <= 0.f) {
            m_frameTimeAvg = renderFrameTime;
        }
        m_frameTimeAvg = m_frameTimeAvg * 0.8f + renderFrameTime * 0.2f;
        if (m_frameTimeAvg > budget) {
            m_autoFastFrames = 0;
            if (++m_autoSlowFrames > 15) {
                m_autoFrameskip = 1;
                m_autoSlowFrames = 0;
            }
        } else {
            m_autoSlowFrames = 0;
            if (++m_autoFastFrames > 90) {
                m_autoFrameskip = 0;
                m_autoFastFrames = 0;
            }
        }
    }
#endif

    if (frameskip > skip) {
        video->getTexture()->unlock();
        frameskip = 0;
    }

    if (audio) {
#if 0
        int queued = audio->getSampleBufferQueued();
        int capacity = audio->getSampleBufferCapacity();
        if (audio->getSamples() + queued > capacity) {
            printf("WARNING: samples: %i, queued: %i, capacity: %i (fps: %f)\n",
                   audio->getSamples(), queued, capacity, targetFps);
        }
#endif
        audio->play(pBurnSoundOut, audio->getSamples(),
                    audio_sync ? Audio::SyncMode::LowLatency : Audio::SyncMode::None);
    }

    UiEmu::onUpdate();
}
