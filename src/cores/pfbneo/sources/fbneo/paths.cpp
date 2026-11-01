// Paths module
#include "cross2d/c2d.h"

// c2d.h (renderer.h) forces MAX_PATH=512, but fbneo's burn.h declares the
// path globals as TCHAR[MAX_PATH] with MAX_PATH=260 in every TU that
// references them (via burnint.h). Force 260 BEFORE burnint.h so the extern
// declarations and the definitions below are consistent - otherwise the LTO
// link emits -Wodr array-bounds warnings.
#undef MAX_PATH
#define MAX_PATH 260

#include "burnint.h"
#include "burner.h"
#include "version.h"

#define s(A) #A
#define a(A) s(A)
#define VERSION a(VER_MAJOR.VER_MINOR.VER_BETA.VER_ALPHA)
char szAppBurnVer[16] = VERSION;

// pfbneo own path buffers: bigger than fbneo's MAX_PATH (260) to be safe
// with long homebrew data paths.
#define PEMU_PATH_SIZE 512

// pfbneo
char szAppHomePath[PEMU_PATH_SIZE];
char szAppRomPath[PEMU_PATH_SIZE];
char szAppSavePath[PEMU_PATH_SIZE];
char szAppConfigPath[PEMU_PATH_SIZE];
char szAppIconPath[PEMU_PATH_SIZE];
// fbneo shared globals: MUST match burn.h extern declarations exactly
// (TCHAR[MAX_PATH] == char[260], MAX_PATH comes from burn.h), otherwise the
// LTO link emits -Wodr warnings (array bounds mismatch).
TCHAR szAppHiscorePath[MAX_PATH];
TCHAR szAppSamplesPath[MAX_PATH];
TCHAR szAppBlendPath[MAX_PATH];
TCHAR szAppEEPROMPath[MAX_PATH];
TCHAR szAppHDDPath[MAX_PATH];
TCHAR szAppSnesMsu1Path[MAX_PATH];

void BurnPathsInit(c2d::C2DIo *io) {
    printf("BurnPathsInit: dataPath = %s\n", io->getDataPath().c_str());

    snprintf(szAppHomePath, PEMU_PATH_SIZE - 1, "%s", io->getDataPath().c_str());
    io->create(szAppHomePath);

    // NOTE: roms are loaded from the UI-configured romsPath (see bzip.cpp),
    // NOT from szAppRomPath, and the SNES MSU-1 media dir is only needed by
    // users of MSU-1 patched snes roms - so do NOT auto-create either one
    // here (an unwanted "arcade"/"snesmsu1" folder used to appear next to
    // the nro on every boot).
    snprintf(szAppRomPath, PEMU_PATH_SIZE - 1, "%s%s/", szAppHomePath, "arcade");
    snprintf(szAppSnesMsu1Path, MAX_PATH - 1, "%s%s/", szAppHomePath, "snesmsu1");

    snprintf(szAppSavePath, PEMU_PATH_SIZE - 1, "%s%s/", szAppHomePath, "saves");
    io->create(szAppSavePath);
    //printf("szAppSavePath: %s\n", szAppSavePath);

    snprintf(szAppConfigPath, PEMU_PATH_SIZE - 1, "%s%s/", szAppHomePath, "configs");
    io->create(szAppConfigPath);
    //printf("szAppConfigPath: %s\n", szAppConfigPath);

    snprintf(szAppSamplesPath, MAX_PATH - 1, "%s%s/", szAppHomePath, "samples");
    io->create(szAppSamplesPath);
    //printf("szAppSamplesPath: %s\n", szAppSamplesPath);

    snprintf(szAppIconPath, PEMU_PATH_SIZE - 1, "%s%s/", szAppHomePath, "icons");
    io->create(szAppIconPath);
    //printf("szAppIconPath: %s\n", szAppIconPath);

    snprintf(szAppBlendPath, MAX_PATH - 1, "%s%s/", szAppHomePath, "blend");
    io->create(szAppBlendPath);
    //printf("szAppBlendPath: %s\n", szAppBlendPath);

    snprintf(szAppHDDPath, MAX_PATH - 1, "%s%s/", szAppHomePath, "hdd");
    io->create(szAppHDDPath);
    //printf("szAppHDDPath: %s\n", szAppHDDPath);
	

    snprintf(szAppEEPROMPath, MAX_PATH - 1, "%s%s/", szAppHomePath, "eeproms");
    io->create(szAppEEPROMPath);

    snprintf(szAppHiscorePath, MAX_PATH - 1, "%s%s/", szAppHomePath, "hiscores");
    // copy hiscore.dat from romfs to datadir
    io->copy(io->getRomFsPath() + "hiscores/", std::string(szAppHiscorePath) + "/");
}
