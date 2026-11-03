//
// pfbneo IPS patch module implementation (see pfbneo_ips.h).
//

// NOTE on include order: "skeleton/pemu.h" MUST come first, same rule as
// pfbneo_keybind.cpp / pfbneo_dip.cpp / pfbneo_memcard.cpp (it pulls in
// ss_api.h, cross2d/c2d.h, pemu_config.h, ui_text_def.h AND the
// "using namespace ss_api").

#include "skeleton/pemu.h"
#include "pfbneo_ips.h"
#include "pfbneo_utility.h"
#include "burner.h"
#include "burnint.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// core IPS symbols (definitions replace the disabled sdl/ips_manager.cpp
// stubs; declared extern at GLOBAL scope in burn/burn.h, read by
// burn/load.cpp and the drivers). these must stay global: a definition inside
// the pfbneo_ips namespace would be a different symbol and the core link fails.
// ---------------------------------------------------------------------------
bool bDoIpsPatch = false;
UINT32 nIpsDrvDefine = 0;
UINT32 nIpsMemExpLen[SND2_ROM + 1] = {0};

namespace pfbneo_ips {

// ---------------------------------------------------------------------------
// module state
// ---------------------------------------------------------------------------
static c2d::config::Config *s_gameConfig = nullptr;
static std::string s_romName;
static bool s_hasIps = false;

// one config option per discovered .dat (sorted by file name, so the option
// ids stay stable as long as the file set does not change)
static std::vector<c2d::config::Option *> s_datOpts;
static std::vector<std::string> s_datFiles;

// ordered patch list built by IpsPatchInit (early includes + enabled dats +
// last includes); IpsApplyPatches walks it in order
static std::vector<std::string> s_patchList;

// dialog row model: category headers (non-selectable) + dat rows
struct Row {
    bool category = false;
    std::string label;
    int datIdx = -1;   // index into s_datFiles, -1 for category rows
};
static std::vector<Row> s_rows;

// ---------------------------------------------------------------------------
// path helpers
// ---------------------------------------------------------------------------
static std::string dataPath() {
    if (c2d_renderer && c2d_renderer->getIo()) {
        std::string p = c2d_renderer->getIo()->getDataPath();
        return p.empty() ? "./" : p;
    }
    return "./";
}

static std::string ipsBase() {
    return dataPath() + "ips/";
}

static std::string ipsDir() {
    return ipsBase() + s_romName + "/";
}

static bool fileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG);
}

// ---------------------------------------------------------------------------
// .dat description (title) extraction: prefer [zh_CN], fall back to [en_US],
// then the first section; "" when the file has no info section
// ---------------------------------------------------------------------------
static std::string getDatTitle(const std::string &path) {
    FILE *fp = fopen(path.c_str(), "rt");
    if (!fp) return {};
    std::string zhTitle, enTitle, otherTitle;
    bool inZH = false, inEN = false, inOther = false;
    bool otherSeen = false;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty() && s[0] == '[') {
            size_t end = s.find(']');
            std::string sec = (end == std::string::npos) ? s.substr(1) : s.substr(1, end - 1);
            inZH = (sec == "zh_CN");
            inEN = (sec == "en_US");
            inOther = !inZH && !inEN;
            otherSeen = otherSeen || inOther;
            continue;
        }
        if (s.empty()) continue;
        if (inZH && zhTitle.empty()) zhTitle = s;
        if (inEN && enTitle.empty()) enTitle = s;
        if (inOther && otherTitle.empty()) otherTitle = s;
        if (!zhTitle.empty() && !enTitle.empty() && !otherTitle.empty()) break;
    }
    fclose(fp);
    if (!zhTitle.empty()) return zhTitle;
    if (!enTitle.empty()) return enTitle;
    if (otherSeen && !otherTitle.empty()) return otherTitle;
    return {};
}

// ---------------------------------------------------------------------------
// .dat discovery (called from onGameConfigCreate)
// ---------------------------------------------------------------------------
static void scanDatDir() {
    s_datFiles.clear();
    DIR *d = opendir(ipsDir().c_str());
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != nullptr) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            size_t dot = name.rfind('.');
            if (dot == std::string::npos) continue;
            std::string ext = name.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".dat") continue;
            s_datFiles.push_back(name);
        }
        closedir(d);
        std::sort(s_datFiles.begin(), s_datFiles.end());
    }
}

// split a "category/name" title into a category row + a patch row
static void buildRows() {
    s_rows.clear();
    if (s_datFiles.empty()) return;

    // gather (category, name, datIdx), category = text before the first '/'
    struct DatInfo {
        std::string category;
        std::string name;
        int datIdx;
    };
    std::vector<DatInfo> dats;
    for (size_t i = 0; i < s_datFiles.size(); i++) {
        std::string title = getDatTitle(ipsDir() + s_datFiles[i]);
        if (title.empty()) title = s_datFiles[i];
        DatInfo info;
        info.datIdx = (int) i;
        size_t slash = title.find('/');
        if (slash != std::string::npos) {
            info.category = title.substr(0, slash);
            info.name = title.substr(slash + 1);
        } else {
            info.category.clear();
            info.name = title;
        }
        dats.push_back(info);
    }
    // stable order: categories first (sorted), then patch names
    std::stable_sort(dats.begin(), dats.end(),
                     [](const DatInfo &a, const DatInfo &b) {
                         if (a.category != b.category) return a.category < b.category;
                         return a.name < b.name;
                     });

    std::string curCat;
    for (auto &info : dats) {
        if (!info.category.empty() && info.category != curCat) {
            // a category header row (non-selectable)
            curCat = info.category;
            Row r;
            r.category = true;
            r.label = info.category;
            r.datIdx = -1;
            s_rows.push_back(r);
        }
        Row r;
        r.category = false;
        r.label = info.name;
        r.datIdx = info.datIdx;
        s_rows.push_back(r);
    }
}

// ---------------------------------------------------------------------------
// .dat patch-entry parsing (mirrors win32 DoPatchGame)
// ---------------------------------------------------------------------------
static const char *IPS_SIGNATURE = "PATCH";
static const char *IPS_TAG_EOF = "EOF";
static const char *IPS_EXT = ".ips";

#define BYTE3_TO_UINT(bp) \
    ((((UINT32)(bp)[0] << 16) & 0x00FF0000) | \
     (((UINT32)(bp)[1] <<  8) & 0x0000FF00) | \
     (( UINT32)(bp)[2]        & 0x000000FF))

#define BYTE2_TO_UINT(bp) \
    ((((UINT32)(bp)[0] << 8) & 0xFF00) | \
     (( UINT32)(bp)[1]       & 0x00FF))

// strqtoken() - functionally identical to strtok() with special treatment for
// quoted strings (dink april 2023, win32 ips_manager.cpp)
static char *strqtoken(char *s, const char *delims) {
    static char *prev_str = nullptr;
    char *token = nullptr;

    if (!s) s = prev_str;
    s += strspn(s, delims);
    if (s[0] == '\0') {
        prev_str = s;
        return nullptr;
    }

    if (s[0] == '"') {
        token = ++s;
        if ((s = strpbrk(token, "\""))) {
            *(s++) = '\0';
        }
    } else {
        token = s;
    }

    if ((s = strpbrk(s, delims))) {
        *(s++) = '\0';
        prev_str = s;
    } else {
        prev_str = (char *) memchr((void *) token, '\0', 1024);
    }

    return token;
}

static int nRomOffset = 0;

// apply one .ips binary file to base (readonly = scan only, fills
// nIpsMemExpLen[LOAD_ROM] with the max patched offset)
static void PatchFile(const std::string &ips_path, UINT8 *base, bool readonly) {
    char buf[6];
    FILE *f = fopen(ips_path.c_str(), "rb");
    if (!f) {
        printf("IPS - can't open file %s, aborting.\n", ips_path.c_str());
        return;
    }

    memset(buf, 0, sizeof(buf));
    fread(buf, 1, 5, f);
    if (strcmp(buf, IPS_SIGNATURE)) {
        printf("IPS - bad IPS-Signature in: %s.\n", ips_path.c_str());
        fclose(f);
        return;
    }

    printf("IPS - patching with: %s (%s)\n", ips_path.c_str(), readonly ? "read-only" : "write");
    INT32 Offset = 0;
    while (!feof(f)) {
        fread(buf, 1, 3, f);
        buf[3] = 0;
        if (strcmp(buf, IPS_TAG_EOF) == 0) break;

        Offset = BYTE3_TO_UINT(buf);

        fread(buf, 1, 2, f);
        INT32 Size = BYTE2_TO_UINT(buf);

        INT32 bRLE = (Size == 0);
        UINT8 ch = 0;
        if (bRLE) {
            fread(buf, 1, 2, f);
            Size = BYTE2_TO_UINT(buf);
            ch = (UINT8) fgetc(f);
        }

        while (Size--) {
            if (!readonly) {
                UINT8 *mem8 = base + Offset + nRomOffset;
                *mem8 = bRLE ? ch : (UINT8) fgetc(f);
            } else {
                if (!bRLE) fgetc(f);
            }
            Offset++;
        }
    }

    // avoid memory out-of-bounds due to an ips offset greater than rom length
    if (readonly && (0 == nIpsMemExpLen[EXP_FLAG])) {
        nIpsMemExpLen[LOAD_ROM] = Offset;
    }

    fclose(f);
}

static bool stristr_int(const char *str1, const char *str2) {
    if (!str2[0]) return true;
    for (; *str1; str1++) {
        const char *p1 = str1;
        const char *p2 = str2;
        while (*p1 && *p2 && tolower((UINT8) *p1) == tolower((UINT8) *p2)) {
            p1++;
            p2++;
        }
        if (!*p2) return true;
    }
    return false;
}

static UINT32 hexto32(const char *s) {
    UINT32 val = 0;
    char c;
    while ((c = *s++)) {
        UINT8 v = ((c & 0xf) + (c >> 6)) | ((c >> 3) & 0x8);
        val = (val << 4) | (UINT32) v;
    }
    return val;
}

static void DoPatchGame(const std::string &patch_name, const char *game_name, UINT32 crc,
                        UINT8 *base, bool readonly) {
    FILE *fp = fopen(patch_name.c_str(), "rt");
    if (!fp) return;

    char s[1024];
    while (fgets(s, sizeof(s), fp)) {
        char *p = s;
        // skip UTF-8 BOM
        if (strncmp(p, "\xef\xbb\xbf", 3) == 0) p += 3;
        if (p[0] == '[') break;   // reached the info section

        // "rom name.bin" "patch file.ips" [IPS_OFFSET_xxx] [crc(abcd1234)]
        char *rom_name = strqtoken(p, " \t\r\n");
        if (!rom_name) continue;
        if (*rom_name == '#') continue;

        char *ips_name = strqtoken(nullptr, " \t\r\n");
        if (!ips_name) continue;

        nRomOffset = 0;
        char *ips_offs = strqtoken(nullptr, " \t\r\n()");
        if (ips_offs) {
            if      (strcmp(ips_offs, "IPS_OFFSET_016") == 0) nRomOffset = 0x1000000;
            else if (strcmp(ips_offs, "IPS_OFFSET_032") == 0) nRomOffset = 0x2000000;
            else if (strcmp(ips_offs, "IPS_OFFSET_048") == 0) nRomOffset = 0x3000000;
            else if (strcmp(ips_offs, "IPS_OFFSET_064") == 0) nRomOffset = 0x4000000;
            else if (strcmp(ips_offs, "IPS_OFFSET_080") == 0) nRomOffset = 0x5000000;
            else if (strcmp(ips_offs, "IPS_OFFSET_096") == 0) nRomOffset = 0x6000000;
            else if (strcmp(ips_offs, "IPS_OFFSET_112") == 0) nRomOffset = 0x7000000;
            else if (strcmp(ips_offs, "IPS_OFFSET_128") == 0) nRomOffset = 0x8000000;
            else if (strcmp(ips_offs, "IPS_OFFSET_144") == 0) nRomOffset = 0x9000000;
        }

        UINT32 nIps_crc = 0;
        if (ips_offs && nRomOffset != 0) {
            ips_offs = strqtoken(nullptr, " \t\r\n()");
        }
        if (ips_offs && stristr_int(ips_offs, "crc")) {
            char *ips_crc = strqtoken(nullptr, " \t\r\n()");
            if (ips_crc) {
                nIps_crc = hexto32(ips_crc);
            }
        }

        // name must match (case-insensitive) or the crc must match
        if (strcmp(rom_name, game_name) != 0 && nIps_crc != crc) continue;

        // resolve the ips path (same rules as the Windows frontend)
        std::string ips_name_str(ips_name);
        bool has_ext = stristr_int(ips_name_str.c_str(), ".ips");
        std::string ips_path;
        if (ips_name[0] == '/' || ips_name[0] == '\\'
            || (ips_name[0] != '\0' && ips_name[1] == ':')) {
            // absolute path
            ips_path = ips_name;
        } else if (strncmp(ips_name, "./", 2) == 0 || strncmp(ips_name, ".\\", 2) == 0) {
            ips_path = ipsDir() + (ips_name + 2);
        } else if (strncmp(ips_name, "../", 3) == 0 || strncmp(ips_name, "..\\", 3) == 0) {
            ips_path = ipsBase() + (ips_name + 3);
        } else if (strchr(ips_name, '/') || strchr(ips_name, '\\')) {
            ips_path = ipsDir() + ips_name;
        } else {
            ips_path = ipsDir() + ips_name;
        }
        if (!has_ext) ips_path += IPS_EXT;

        PatchFile(ips_path, base, readonly);
    }
    fclose(fp);
}

// ---------------------------------------------------------------------------
// #define / #include parsing (mirrors win32 GetIpsDrvDefine / ExtraPatchesInit)
// ---------------------------------------------------------------------------
static UINT32 GetIpsDefineExpValue(char *pszTmp) {
    pszTmp = strqtoken(nullptr, " \t\r\n");
    if (!pszTmp) return 0U;

    INT32 nRet = 0;
    if      (strcmp(pszTmp, "EXP_VALUE_001") == 0) nRet = 0x0010000;
    else if (strcmp(pszTmp, "EXP_VALUE_002") == 0) nRet = 0x0020000;
    else if (strcmp(pszTmp, "EXP_VALUE_003") == 0) nRet = 0x0030000;
    else if (strcmp(pszTmp, "EXP_VALUE_004") == 0) nRet = 0x0040000;
    else if (strcmp(pszTmp, "EXP_VALUE_005") == 0) nRet = 0x0050000;
    else if (strcmp(pszTmp, "EXP_VALUE_006") == 0) nRet = 0x0060000;
    else if (strcmp(pszTmp, "EXP_VALUE_007") == 0) nRet = 0x0070000;
    else if (strcmp(pszTmp, "EXP_VALUE_008") == 0) nRet = 0x0080000;
    else if (strcmp(pszTmp, "EXP_VALUE_010") == 0) nRet = 0x0100000;
    else if (strcmp(pszTmp, "EXP_VALUE_020") == 0) nRet = 0x0200000;
    else if (strcmp(pszTmp, "EXP_VALUE_030") == 0) nRet = 0x0300000;
    else if (strcmp(pszTmp, "EXP_VALUE_040") == 0) nRet = 0x0400000;
    else if (strcmp(pszTmp, "EXP_VALUE_050") == 0) nRet = 0x0500000;
    else if (strcmp(pszTmp, "EXP_VALUE_060") == 0) nRet = 0x0600000;
    else if (strcmp(pszTmp, "EXP_VALUE_070") == 0) nRet = 0x0700000;
    else if (strcmp(pszTmp, "EXP_VALUE_080") == 0) nRet = 0x0800000;
    else if (strcmp(pszTmp, "EXP_VALUE_100") == 0) nRet = 0x1000000;
    else if (strcmp(pszTmp, "EXP_VALUE_200") == 0) nRet = 0x2000000;
    else if (strcmp(pszTmp, "EXP_VALUE_300") == 0) nRet = 0x3000000;
    else if (strcmp(pszTmp, "EXP_VALUE_400") == 0) nRet = 0x4000000;
    else if (strcmp(pszTmp, "EXP_VALUE_500") == 0) nRet = 0x5000000;
    else if (strcmp(pszTmp, "EXP_VALUE_600") == 0) nRet = 0x6000000;
    else if (strcmp(pszTmp, "EXP_VALUE_700") == 0) nRet = 0x7000000;
    else if (strcmp(pszTmp, "EXP_VALUE_800") == 0) nRet = 0x8000000;
    else if (sscanf(pszTmp, "%x", &nRet) != EOF) return nRet;

    return (UINT32) nRet;
}

// scan one dat for #define lines, updating nIpsDrvDefine / nIpsMemExpLen
static void scanDefines(const std::string &path) {
    FILE *fp = fopen(path.c_str(), "rt");
    if (!fp) return;

    char str[1024];
    while (fgets(str, sizeof(str), fp)) {
        char *ptr = str;
        if (strncmp(ptr, "\xef\xbb\xbf", 3) == 0) ptr += 3;

        char *tmp = strqtoken(ptr, " \t\r\n");
        if (!tmp) continue;
        if (tmp[0] == '[') break;
        if (tmp[0] == '/' && tmp[1] == '/') continue;
        if (strcmp(tmp, "#define") != 0) continue;

        tmp = strqtoken(nullptr, " \t\r\n");
        if (!tmp) continue;

        UINT32 nNewValue = 0;
        if      (strcmp(tmp, "IPS_NOT_PROTECT") == 0)  { nIpsDrvDefine |= IPS_NOT_PROTECT; }
        else if (strcmp(tmp, "IPS_PGM_SPRHACK") == 0)  { nIpsDrvDefine |= IPS_PGM_SPRHACK; }
        else if (strcmp(tmp, "IPS_PGM_MAPHACK") == 0)  { nIpsDrvDefine |= IPS_PGM_MAPHACK; }
        else if (strcmp(tmp, "IPS_PGM_SNDOFFS") == 0)  { nIpsDrvDefine |= IPS_PGM_SNDOFFS; }
        else if (strcmp(tmp, "IPS_SNES_VRAMHK") == 0)  { nIpsDrvDefine |= IPS_SNES_VRAMHK; }
        else if (strcmp(tmp, "IPS_NEO_RAMHACK") == 0)  { nIpsDrvDefine |= IPS_NEO_RAMHACK; }
        else if (strcmp(tmp, "IPS_LOAD_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_LOAD_EXPAND;
            nIpsMemExpLen[EXP_FLAG] = 1;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[LOAD_ROM])
                nIpsMemExpLen[LOAD_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_EXTROM_INCL") == 0)  {
            nIpsDrvDefine |= IPS_EXTROM_INCL;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[EXTR_ROM])
                nIpsMemExpLen[EXTR_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_PRG1_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_PRG1_EXPAND;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[PRG1_ROM])
                nIpsMemExpLen[PRG1_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_PRG2_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_PRG2_EXPAND;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[PRG2_ROM])
                nIpsMemExpLen[PRG2_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_GRA1_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_GRA1_EXPAND;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[GRA1_ROM])
                nIpsMemExpLen[GRA1_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_GRA2_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_GRA2_EXPAND;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[GRA2_ROM])
                nIpsMemExpLen[GRA2_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_GRA3_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_GRA3_EXPAND;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[GRA3_ROM])
                nIpsMemExpLen[GRA3_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_ACPU_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_ACPU_EXPAND;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[ACPU_ROM])
                nIpsMemExpLen[ACPU_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_SND1_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_SND1_EXPAND;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[SND1_ROM])
                nIpsMemExpLen[SND1_ROM] = nNewValue;
        }
        else if (strcmp(tmp, "IPS_SND2_EXPAND") == 0)  {
            nIpsDrvDefine |= IPS_SND2_EXPAND;
            if ((nNewValue = GetIpsDefineExpValue(tmp)) > nIpsMemExpLen[SND2_ROM])
                nIpsMemExpLen[SND2_ROM] = nNewValue;
        }
    }
    fclose(fp);
}

// collect #include "xxx" dats: before any patch-entry line -> early, after -> last
static void collectIncludes(const std::string &path,
                            std::vector<std::string> &early, std::vector<std::string> &last) {
    FILE *fp = fopen(path.c_str(), "rt");
    if (!fp) return;

    bool sawPatch = false;
    char str[1024];
    while (fgets(str, sizeof(str), fp)) {
        char *ptr = str;
        if (strncmp(ptr, "\xef\xbb\xbf", 3) == 0) ptr += 3;

        char *tmp = strqtoken(ptr, " \t\r\n");
        if (!tmp) continue;
        if (tmp[0] == '[') break;
        if (tmp[0] == '/' && tmp[1] == '/') continue;
        if (strcmp(tmp, "#include") != 0) {
            sawPatch = true;
            continue;
        }
        tmp = strqtoken(nullptr, " \t\r\n");
        if (!tmp) continue;
        // only same-directory dats are allowed
        if (strchr(tmp, '/') || strchr(tmp, '\\')) continue;
        std::string inc(tmp);
        if (inc.rfind('.') == std::string::npos) inc += ".dat";
        std::string incPath = ipsDir() + inc;
        if (!fileExists(incPath)) continue;
        if (sawPatch) {
            last.push_back(incPath);
        } else {
            early.push_back(incPath);
        }
    }
    fclose(fp);
}

// ---------------------------------------------------------------------------
// core implementations (namespace-internal; the global IpsPatchInit /
// IpsApplyPatches / IpsPatchExit entry points required by burn.h and
// burn/load.cpp are thin wrappers defined after the namespace closes)
// ---------------------------------------------------------------------------
static void ipsPatchInitImpl() {
    if (!bDoIpsPatch) return;

    nIpsDrvDefine = 0;
    memset(nIpsMemExpLen, 0, sizeof(nIpsMemExpLen));
    s_patchList.clear();

    // enabled dats (in stable file order)
    std::vector<std::string> enabled;
    for (size_t i = 0; i < s_datOpts.size() && i < s_datFiles.size(); i++) {
        if (s_datOpts[i] && s_datOpts[i]->getInteger()) {
            enabled.push_back(ipsDir() + s_datFiles[i]);
        }
    }
    if (enabled.empty()) {
        bDoIpsPatch = false;
        return;
    }

    // collect early/last includes, then build the ordered patch list
    std::vector<std::string> early, last;
    for (auto &dat : enabled) {
        collectIncludes(dat, early, last);
    }
    s_patchList = early;
    for (auto &dat : enabled) s_patchList.push_back(dat);
    for (auto &dat : last) s_patchList.push_back(dat);

    // scan every patch list entry for #define expansion/hack flags
    for (auto &dat : s_patchList) {
        scanDefines(dat);
    }
}

static void ipsApplyPatchesImpl(UINT8 *base, char *rom_name, UINT32 crc, bool readonly) {
    if (!bDoIpsPatch) return;
    for (auto &dat : s_patchList) {
        DoPatchGame(dat, rom_name, crc, base, readonly);
    }
}

static void ipsPatchExitImpl() {
    memset(nIpsMemExpLen, 0, sizeof(nIpsMemExpLen));
    nIpsDrvDefine = 0;
    bDoIpsPatch = false;
    s_patchList.clear();
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------
void init() {
    s_gameConfig = nullptr;
    s_romName.clear();
    s_hasIps = false;
    s_datOpts.clear();
    s_datFiles.clear();
    s_rows.clear();
    ipsPatchExitImpl();
}

void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig) {
    s_gameConfig = gameConfig;
    s_datOpts.clear();
    s_datFiles.clear();
    s_rows.clear();
    s_hasIps = false;
    s_romName.clear();

    PFBNEOUtility::setDriverActive(game);
    if (nBurnDrvActive >= nBurnDrvCount) return;
    const char *name = BurnDrvGetTextA(DRV_NAME);
    if (!name || !name[0]) return;
    s_romName = name;

    scanDatDir();
    if (s_datFiles.empty()) return;
    s_hasIps = true;

    // per-rom "IPS" group: one bool option per dat file
    c2d::config::Group group("IPS", IPS_GROUP_ID, TEXT_MENU_IPS);
    for (size_t i = 0; i < s_datFiles.size(); i++) {
        std::string key = "IPS_DAT_" + std::to_string((long long) i);
        group.addOption({key, 0, IPS_OPT_BASE + (int) i, s_datFiles[i], TEXT_MENU_IPS});
    }
    gameConfig->addGroup(group);
    for (size_t i = 0; i < s_datFiles.size(); i++) {
        s_datOpts.push_back(gameConfig->getOption(IPS_OPT_BASE + (int) i));
    }

    buildRows();
}

// called right before DrvInit: read the (already restored) enabled state and
// arm the core so BurnLoadRom patches the roms while loading
void applyEnabled() {
    if (!s_hasIps) {
        bDoIpsPatch = false;
        s_patchList.clear();
        return;
    }
    bDoIpsPatch = false;
    for (auto *opt : s_datOpts) {
        if (opt && opt->getInteger()) {
            bDoIpsPatch = true;
            break;
        }
    }
    if (bDoIpsPatch) {
        ipsPatchInitImpl();
    }
}

void onGameStop() {
    ipsPatchExitImpl();
    s_gameConfig = nullptr;
    s_datOpts.clear();
    s_datFiles.clear();
    s_rows.clear();
    s_hasIps = false;
    s_romName.clear();
}

// ---------------------------------------------------------------------------
// feature detection
// ---------------------------------------------------------------------------
bool hasIps() {
    return s_hasIps;
}

const char *getRomName() {
    return s_romName.c_str();
}

// ---------------------------------------------------------------------------
// dialog row model
// ---------------------------------------------------------------------------
int getRowCount() {
    return (int) s_rows.size();
}

bool isCategoryRow(int row) {
    return row >= 0 && row < (int) s_rows.size() && s_rows[row].category;
}

const char *getRowName(int row) {
    if (row < 0 || row >= (int) s_rows.size()) return "";
    return s_rows[row].label.c_str();
}

bool isRowEnabled(int row) {
    if (row < 0 || row >= (int) s_rows.size()) return false;
    int idx = s_rows[row].datIdx;
    if (idx < 0 || idx >= (int) s_datOpts.size()) return false;
    c2d::config::Option *opt = s_datOpts[idx];
    return opt && opt->getInteger() != 0;
}

void toggleRow(int row) {
    if (row < 0 || row >= (int) s_rows.size()) return;
    int idx = s_rows[row].datIdx;
    if (idx < 0 || idx >= (int) s_datOpts.size()) return;
    c2d::config::Option *opt = s_datOpts[idx];
    if (opt) {
        opt->setInteger(opt->getInteger() ? 0 : 1);
    }
}

} // namespace pfbneo_ips

// ---------------------------------------------------------------------------
// global entry points required by burn/burn.h (called by burn/load.cpp while
// roms load and by the pfbneo lifecycle): thin wrappers over the module
// ---------------------------------------------------------------------------
void IpsPatchInit() {
    pfbneo_ips::ipsPatchInitImpl();
}

void IpsApplyPatches(UINT8 *base, char *rom_name, UINT32 crc, bool readonly) {
    pfbneo_ips::ipsApplyPatchesImpl(base, rom_name, crc, readonly);
}

void IpsPatchExit() {
    pfbneo_ips::ipsPatchExitImpl();
}