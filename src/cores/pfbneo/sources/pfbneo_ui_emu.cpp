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
#include "pfbneo_turbo.h"
#include "pfbneo_ui_cheat.h"
#include "fbneo/pfbneo_cheat.h"
#include "retro_input_wrapper.h"

// tiny helper to trace the cheat dialog (log file: /switch/cheat_emu.log,
// controlled by the master switch PEMU_ENABLE_DEBUG_LOG in pemu.h, OFF by default)
#if PEMU_ENABLE_DEBUG_LOG
static void cheatLog2(const char *fmt, ...) {
    FILE *f = fopen("/switch/cheat_emu.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fputc('\n', f);
        fclose(f);
    }
}
#else
static void cheatLog2(const char *fmt, ...) { }
#endif

using namespace c2d;
using namespace pemu;

int nVidFullscreen = 0;
INT32 bVidUseHardwareGamma = 1;

UINT32 (__cdecl *VidHighCol)(INT32 r, INT32 g, INT32 b, INT32 i);

INT32 VidRecalcPal() { return BurnRecalcPal(); }

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
}

bool PFBAUiEmu::isExtraDialogOpen() {
    return cheatDialog && cheatDialog->isVisible();
}

void PFBAUiEmu::attachToUi() {
    if (cheatDialog && !m_dialogAttached) {
        pMain->add(cheatDialog);
        m_dialogAttached = true;
        cheatLog2("attachToUi: dialog attached to ui tree, dialogPtr=%p", (void *) cheatDialog);
    }
}

bool PFBAUiEmu::onMenuExtra(const std::string &name) {
    if (name == TEXT_CHEATS) {
        // rom menu -> 金手指: open the cheat dialog (game is running)
        if (cheatDialog) {
            cheatLog2("onMenuExtra: opening cheat dialog");
            cheatDialog->open();
        } else {
            cheatLog2("onMenuExtra: cheatDialog is NULL!");
        }
        return true;
    }
    return false;
}

#ifdef __PFBA_ARM__

int PFBAUiEmu::getSekCpuCore() {
    int sekCpuCore = 0; // SEK_CORE_C68K: USE CYCLONE ARM ASM M68K CORE

    std::vector<std::string> zipList;
    int hardware = BurnDrvGetHardwareCode();

    std::string bios = pMain->getConfig()->get(PEMUConfig::OptId::EMU_NEOBIOS, true)->getString();
    if (isHardware(hardware, HARDWARE_PREFIX_SNK) && Utility::contains(bios, "UNIBIOS")) {
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
                                                   pMain->getIo()->getRomFsPath() + "langs/gamelist.bin");
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
    // some drivers require audio buffer to be allocated for DrvInit, add a "dummy" one for now...
    auto *aud = new Audio(audio_freq);
    nBurnSoundRate = aud->getSampleRate();
    nBurnSoundLen = aud->getSamples();
    pBurnSoundOut = (INT16 *) malloc(aud->getSamplesSize());
    if (DrvInit((int) nBurnDrvActive, false) != 0) {
        printf("\nPFBAUiEmu::load: driver initialisation failed\n");
        delete (aud);
        pMain->getUiProgressBox()->setVisibility(Visibility::Hidden);
        pMain->getUiMessageBox()->show(TEXT_MSG_TITTLE_ERROR, TEXT_MSG_DRIVER_INIT_FAILED, TEXT_BUTTON_OK);
        stop();
        return -1;
    }
    delete (aud);
    free(pBurnSoundOut);
    nFramesEmulated = 0;
    nFramesRendered = 0;
    nCurrentFrame = 0;
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

    // pfbneo turbo: reset counters + log game start
    pfbneo_turbo::onGameStart();

    // pfbneo cheat: parse cheats/<rom>.ini and rebuild the core list.
    // runtime-only: nothing is persisted, all cheats start inactive.
    pfbneo_cheat::load(game, pMain->getIo());

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

void PFBAUiEmu::stop() {
    // pfbneo turbo: log game stop and clear runtime state
    pfbneo_turbo::onGameStop();
    // pfbneo cheat: clear runtime state (the core's CheatExit() already
    // freed the pCheatInfo list during DrvExit); hide the dialog if open
    if (cheatDialog) {
        cheatDialog->setVisibility(Visibility::Hidden, true);
    }
    pfbneo_cheat::reset();
    DrvExit();
    if (pBurnSoundOut) {
        free(pBurnSoundOut);
    }
    UiEmu::stop();
}

bool PFBAUiEmu::onInput(c2d::Input::Player *players) {
    if (pMain->getUiMenu()->isVisible() || pMain->getUiStateMenu()->isVisible()) {
        pMain->getInput()->setRotation(Input::Rotation::R0, Input::Rotation::R0);
        return UiEmu::onInput(players);
    }

    // pfbneo cheat dialog: while it is open, let the dialog (added after the
    // emu in the ui tree) handle all input instead of the running game
    if (cheatDialog && cheatDialog->isVisible()) {
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

    // update fbneo video buffer and audio
#ifdef __VITA__
    int skip = pMain->getConfig()->get(PEMUConfig::OptId::EMU_FRAMESKIP, true)->getInteger();
#else
    int skip = 0;
#endif

    pBurnDraw = nullptr;
    frameskip++;

    if (frameskip > skip) {
        video->getTexture()->lock(&pBurnDraw, &nBurnPitch);
        nFramesRendered++;
    }

    BurnDrvFrame();
    nCurrentFrame++;

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
