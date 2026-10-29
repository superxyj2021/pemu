// Driver Init module

#include "skeleton/pemu.h"
#include "burner.h"
#include "retro_input.h"

using namespace pemu;

extern UiMain *pemu_ui;

extern UINT8 NeoSystem;
int bDrvOkay = 0;
int kNetGame = 0;
INT32 nInputIntfMouseDivider = 1;
bool bRunPause;

bool is_netgame_or_recording() {
    return false;
}

static int ProgressCreate();

static UINT8 NeoSystemList[] = {
        0x13, // "Universe BIOS ver. 4.0"
        0x14, // "Universe BIOS ver. 3.3"
        0x15, // "Universe BIOS ver. 3.2"
        0x16, // "Universe BIOS ver. 3.1"
        0x00, // "MVS Asia/Europe ver. 6 (1 slot)"
        0x01, // "MVS Asia/Europe ver. 5 (1 slot)"
        0x02, // "MVS Asia/Europe ver. 3 (4 slot)"
        0x03, // "MVS USA ver. 5 (2 slot)"
        0x04, // "MVS USA ver. 5 (4 slot)"
        0x05, // "MVS USA ver. 5 (6 slot)"
        0x08, // "MVS Japan ver. 6 (? slot)"
        0x09, // "MVS Japan ver. 5 (? slot)"
        0x0a, // "MVS Japan ver. 6 (4 slot)"
        0x10, // "AES Asia"
        0x0f, // "AES Japan"
        0x0b, // "NEO-MVH MV1C (Asia)"
        0x0c, // "NEO-MVH MV1C (Japan)"
        0x12, // "Deck ver. 6 (Git Ver 1.3)"
        0x11, // "Development Kit"
};

static int DoLibInit() {
    int nRet;

    ProgressCreate();

    nRet = BzipOpen(false);
    printf("DoLibInit: BzipOpen = %i\n", nRet);
    if (nRet) {
        BzipClose();
        return 1;
    }

    NeoSystem = NeoSystemList[pemu_ui->getConfig()->get(PEMUConfig::OptId::EMU_NEOBIOS, true)->getArrayIndex()];

    nRet = BurnDrvInit();
    printf("DoLibInit: BurnDrvInit = %i\n", nRet);

    BzipClose();

    if (nRet) {
        BurnDrvExit();
        return 1;
    } else {
        return 0;
    }
}

// Catch calls to BurnLoadRom() once the emulation has started;
// Intialise the zip module before forwarding the call, and exit cleanly.
static int DrvLoadRom(unsigned char *Dest, int *pnWrote, int i) {
    int nRet;

    BzipOpen(false);

    if ((nRet = BurnExtLoadRom(Dest, pnWrote, i)) != 0) {
        char szText[256];
        char *pszFilename;
        BurnDrvGetRomName(&pszFilename, i, 0);
        sprintf(szText, "Error loading %s for %s.\nEmulation will likely have problems.",
                pszFilename, BurnDrvGetTextA(DRV_NAME));
        printf("DrvLoadRom: %s\n", szText);
        pemu_ui->getUiMessageBox()->show("ERROR", szText, "OK");
    }

    BzipClose();

    BurnExtLoadRom = DrvLoadRom;

    return nRet;
}

int DrvInit(int nDrvNum, bool bRestore) {
    printf("DrvInit(%i, %i)\n", nDrvNum, bRestore);
    DrvExit();

    // set selected driver
    nBurnDrvSelect[0] = (UINT32) nDrvNum;
    // for retro_input
    bIsNeogeoCartGame = ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_NEOGEO);
    // default input values
    nMaxPlayers = BurnDrvGetMaxPlayers();
    SetDefaultDeviceTypes();
    // init inputs
    InputInit();
    SetControllerInfo();

    printf("DrvInit: DoLibInit()\n");
    if (DoLibInit()) {                // Init the Burn library's driver
        //char szTemp[512];
        //_stprintf(szTemp, _T("Error starting '%s'.\n"), BurnDrvGetText(DRV_FULLNAME));
        //AppError(szTemp, 1);
        return 1;
    }

    printf("DrvInit: BurnExtLoadRom = DrvLoadRom\n");
    BurnExtLoadRom = DrvLoadRom;

    char path[1024];
    snprintf(path, 1023, "%s%s.fs", szAppEEPROMPath, BurnDrvGetTextA(DRV_NAME));
    BurnStateLoad(path, 0, nullptr);

    bDrvOkay = 1;
    nBurnLayer = 0xff;
    nSpriteEnable = 0xff;

    return 0;
}

// for uiStateMenu.cpp (BurnStateLoad)
int DrvInitCallback() {
    return DrvInit((int) nBurnDrvSelect[0], false);
}

int DrvExit() {
    if (bDrvOkay) {
        if (nBurnDrvSelect[0] < nBurnDrvCount) {
            char path[1024];
            snprintf(path, 1023, "%s%s.fs", szAppEEPROMPath, BurnDrvGetTextA(DRV_NAME));
            BurnStateSave(path, 0);
            InputExit();
            BurnDrvExit();
        }
    }

    BurnExtLoadRom = nullptr;
    bDrvOkay = 0;
    nBurnDrvSelect[0] = ~0U;

    return 0;
}

static double nProgressPosBurn = 0;

// ---- loading progress ----------------------------------------------------
// two independent progress accumulators (same split as the FBNeo frontend):
//   s_burnPos - burner-level absolute progress 0..1 (the zip/rom decompression
//               loop in bzip.cpp calls ProgressUpdateBurner() with 1/N steps)
//   s_corePos - per-segment progress inside the core drivers, which use
//               BurnSetProgressRange() + BurnUpdateProgress() (e.g. the NeoGeo
//               sprite decryption loop): each BurnSetProgressRange() call
//               narrows the current segment lower bound (s_min) and
//               BurnUpdateProgress() fills that segment
static double s_burnPos = 0;
static double s_corePos = 0;
static double s_min = 0;        // negative segment lower bound (0 when idle)
static const double s_max = 1.0;

static c2d::C2DClock s_progressClock;
static float s_lastProgressFlip = 0;

// throttled present: flip at most every 100ms so the progress bar keeps
// moving while minimizing mid-load frames that could catch the font atlas
// being updated with new glyphs.
static void progressApply() {
    double v = s_min + s_corePos + s_burnPos;
    if (v < 0) {
        v = 0;
    }
    if (v > s_max) {
        v = s_max;
    }
    pemu_ui->getUiProgressBox()->setProgress((float) v);

    float now = s_progressClock.getElapsedTime().asSeconds();
    if (now - s_lastProgressFlip >= 0.1f) {
        s_lastProgressFlip = now;
        pemu_ui->flip();
    }
}

// core driver: BurnSetProgressRange(dProgressRange) -> narrow the current
// segment (lower bound = -range); s_corePos continues to fill it
static int ProgressSetRangeBurn(double dProgressRange) {
    s_min = -(s_max * dProgressRange);
    return 0;
}

// core driver: BurnUpdateProgress(dProgress, pszText, bAbs) -> fill the
// current segment (dProgress is relative to the segment, 0..1 or a delta)
static int ProgressUpdateBurn(double dProgress, const TCHAR *pszText, bool bAbs) {
    if (pszText) {
        pemu_ui->getUiProgressBox()->setMessage(pszText);
    }
    if (bAbs) {
        s_corePos = (-s_min) * dProgress;
        if (s_corePos > -s_min) {
            s_corePos = -s_min;
        }
    } else if (dProgress > 0) {
        s_corePos += (-s_min) * dProgress;
        if (s_corePos > -s_min) {
            s_corePos = -s_min;
        }
    }
    progressApply();
    return 0;
}

static int ProgressCreate() {
    s_burnPos = 0;
    s_corePos = 0;
    s_min = 0;
    BurnExtProgressRangeCallback = ProgressSetRangeBurn;
    BurnExtProgressUpdateCallback = ProgressUpdateBurn;
    pemu_ui->getUiProgressBox()->setVisibility(c2d::Visibility::Visible);
    pemu_ui->getUiProgressBox()->setLayer(1000);
    return 0;
}

int ProgressUpdateBurner(double dProgress, const TCHAR *pszText, bool bAbs) {
    // NOTE (loading screen fixes):
    // 1) the title is set ONCE by PFBAUiEmu::load() using the (chinese) game
    //    display name from the rom list. do NOT overwrite it with the english
    //    driver full name here.
    // 2) flip() is done here but THROTTLED (every 100ms): DrvInit() is a
    //    synchronous, blocking call so without any present the progress bar
    //    data updates but never appears on screen. the title and "Please wait"
    //    glyphs are pre-warmed into the font atlas by PFBAUiEmu::load() before
    //    the first frame, so the digits/messages added later rarely trigger a
    //    mid-frame atlas update.

    if (pszText) {
        pemu_ui->getUiProgressBox()->setMessage(pszText);
    } else {
        pemu_ui->getUiProgressBox()->setMessage("Please wait...");
    }
    if (bAbs) {
        s_burnPos = s_max * dProgress;
    } else if (dProgress > 0) {
        s_burnPos += s_max * dProgress;
    }
    if (s_burnPos > s_max) {
        s_burnPos = s_max;
    }
    progressApply();
    return 0;
}

int AppError(TCHAR *szText, int bWarning) {
    //ui->getUiMessageBox()->show("ERROR", szText ? szText : "UNKNOW ERROR", "OK");
    return 1;
}

#ifdef __PFBN_LIGHT__

void nes_add_cheat(char *code) {};

void nes_remove_cheat(char *code) {};
#endif
