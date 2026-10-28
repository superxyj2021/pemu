//
// pfbneo cheat (金手指) support implementation.
// See pfbneo_cheat.h for the file format documentation.
//

// NOTE: "skeleton/pemu.h" must come FIRST (see pfbneo_turbo.cpp notes)
#include "skeleton/pemu.h"
#include "pfbneo_cheat.h"
#include "burner.h"
#include "burnint.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <cstdarg>

namespace pfbneo_cheat {

    // one address write (cpu, address, value). for NES/SNES the database may
    // instead encode the address as a Game Genie / PAR code string (e.g.
    // "pkzeta", "SXNVZPAX", "EDEA-1F6B", "7E0A2A06"); when genie is non-empty
    // it is handed to the NES/SNES core hook (nes_add_cheat) and address/value
    // are ignored.
    struct CheatAddr {
        int cpu = 0;
        unsigned int address = 0;
        unsigned int value = 0;
        std::string genie;
    };

    struct PfbCheat {
        std::string name;
        std::vector<std::string> optionNames;              // compact, in file order
        std::vector<std::vector<CheatAddr>> optionAddresses;
        int selected = 0;                                  // runtime selection
    };

    // hardware type used to pick the global cheat database file:
    //   arcade -> cheat.dat (+ wayder_cheat.dat), NES/FC -> cheatnes.dat,
    //   SNES -> cheatsnes.dat   (same logic as the FBNeo frontend)
#define HW_NES  ( ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_NES)  \
               || ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_FDS) )
#define HW_SNES ( ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNES) )
#define HW_GGENIE ( HW_NES || HW_SNES )

    static std::vector<PfbCheat> s_cheats;
    static bool s_logEnabled = true;

    // ---- logging (write to /switch/cheat_debug.log; controlled by the
    //      master switch PEMU_ENABLE_DEBUG_LOG in pemu.h, OFF by default) ---
#if PEMU_ENABLE_DEBUG_LOG
    static void cheatLog(const char *fmt, ...) {
        if (!s_logEnabled) {
            return;
        }
        static FILE *s_log = nullptr;
        if (!s_log) {
            s_log = fopen("/switch/cheat_debug.log", "a");
        }
        if (s_log) {
            va_list args;
            va_start(args, fmt);
            vfprintf(s_log, fmt, args);
            va_end(args);
            fputc('\n', s_log);
            fflush(s_log);
        }
    }
#else
    static void cheatLog(const char *fmt, ...) { }
#endif

    // ---- ini parser ---------------------------------------------------------

    static std::string trim(char *s) {
        while (*s && isspace((unsigned char) *s)) {
            s++;
        }
        char *end = s + strlen(s);
        while (end > s && isspace((unsigned char) *(end - 1))) {
            end--;
        }
        *end = '\0';
        return std::string(s);
    }

    static bool readQuoted(char *&p, std::string &out) {
        while (*p && isspace((unsigned char) *p)) {
            p++;
        }
        if (*p != '"') {
            return false;
        }
        p++;
        char *end = strchr(p, '"');
        if (!end) {
            return false;
        }
        out.assign(p, (size_t) (end - p));
        p = end + 1;
        return true;
    }

    static bool loadIniFile(const std::string &path) {
        FILE *f = fopen(path.c_str(), "rt");
        if (!f) {
            return false;
        }

        PfbCheat *current = nullptr;
        char line[4096];

        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '/' && line[1] == '/') {
                continue;
            }
            std::string trimmed = trim(line);
            if (trimmed.empty()) {
                continue;
            }
            char *s = line;

            // "cheat" declaration (possibly followed by '{')
            if (strncmp(s, "cheat", 5) == 0 && (s[5] == ' ' || s[5] == '\t')) {
                s += 5;
                while (*s && isspace((unsigned char) *s)) {
                    s++;
                }
                std::string name;
                if (readQuoted(s, name)) {
                    s_cheats.push_back(PfbCheat());
                    current = &s_cheats.back();
                    current->name = name;
                    cheatLog("cheat: '%s'", name.c_str());
                }
                continue;
            }

            if (!current) {
                continue;
            }

            // "type" / "default" labels: not needed (default option 0 is
            // "Disabled" in the standard FBA files; type 0 = apply each frame)

            // option line:  <index> "<name>" cpu,addr,value[,cpu,addr,value...]
            char *p = s;
            int optionIndex = (int) strtol(p, &p, 0);
            if (p == s) {
                continue;
            }
            while (*p && isspace((unsigned char) *p)) {
                p++;
            }
            if (*p == '{') { // closing bracket of a cheat block
                continue;
            }
            std::string optName;
            if (!readQuoted(p, optName)) {
                continue;
            }

            // options are stored compactly in file order (the core's pOption[],
            // the dialog and selectOption all use the same compact indexing)
            if (optionIndex < 0 || optionIndex > 255) {
                continue;
            }

            // address groups: cpu,address,value (comma separated)
            std::vector<CheatAddr> addrs;
            while (*p) {
                while (*p && (*p == ',' || isspace((unsigned char) *p))) {
                    p++;
                }
                if (*p == '\0' || *p == '{' || *p == '}') {
                    break;
                }
                char *t = nullptr;
                CheatAddr a;
                a.cpu = (int) strtol(p, &t, 0);
                if (t == p) {
                    break;
                }
                p = t;
                while (*p && (*p == ',' || isspace((unsigned char) *p))) {
                    p++;
                }
                a.address = (unsigned int) strtoul(p, &t, 0);
                if (t == p) {
                    break;
                }
                p = t;
                while (*p && (*p == ',' || isspace((unsigned char) *p))) {
                    p++;
                }
                a.value = (unsigned int) strtoul(p, &t, 0);
                if (t == p) {
                    break;
                }
                p = t;
                addrs.push_back(a);
            }

            current->optionNames.push_back(optName);
            current->optionAddresses.push_back(addrs);
            cheatLog("  option[%d] '%s' (%zu addr groups)", optionIndex, optName.c_str(), addrs.size());
        }

        fclose(f);
        return !s_cheats.empty();
    }

    // ---- mame dat parser (cheat.dat / cheatnes.dat / cheatsnes.dat) --------
    //
    // Same syntax that the FBNeo frontend (src/burner/conc.cpp,
    // ConfigParseMAMEFile_internal) supports. NOTE: in these database files
    // EVERY entry line carries the rom name itself, there is NO separate
    // ":<romname>:" header line. The frontend matches each line by prefix:
    //   - every entry line is ":<romname>:flags:address:value:attrib:name"
    //   - lines whose prefix is not ":<romname>:" are skipped until the first
    //     match; once a match was found, a non-matching line ends the section
    //   - NES/SNES databases use the driver name with a prefix: ":nes_xxx:",
    //     ":snes_xxx:" (the arcade databases use the plain driver name)
    //   - "----:REASON" marks the end of the database file
    //   - flag bits follow the FBA interpretation:
    //       0x4800            -> skip (unsupported methods)
    //       0x8000 or 0x10000 -> the line adds an address group to the
    //                            current option (linked cheat)
    //       0x10000 + menu    -> starts a new option of the current cheat
    //       0x100 + menu      -> "add options": auto-generate "# 1..# N"
    //       otherwise         -> starts a new cheat
    //   - the first option is always "Disabled" with no address.
    //
    // Parsed cheats are appended to the same s_cheats list used by the ini
    // loader, so both formats can be loaded for one game.
    static bool isHexString(const std::string &s) {
        if (s.empty()) {
            return false;
        }
        for (char c : s) {
            if (!isxdigit((unsigned char) c)) {
                return false;
            }
        }
        return true;
    }

    static bool parseMameEntry(const char *line, unsigned int &flags,
                               unsigned int &address, unsigned int &value,
                               unsigned int &attrib, std::string &name,
                               std::string &addrRaw) {
        // find up to 6 colons, ignoring colons inside double quotes
        const char *f[6] = {nullptr};
        int colon = 0;
        bool inQuote = false;
        for (const char *q = line; *q && colon < 6; q++) {
            if (*q == '"') {
                inQuote = !inQuote;
            } else if (*q == ':' && !inQuote) {
                f[colon++] = q;
            }
        }
        if (colon < 6) {
            return false;
        }
        auto field = [&](int a, int b) -> std::string {
            const char *start = f[a] + 1;
            const char *end = (b < 6 && f[b] != nullptr) ? f[b] : start + strlen(start);
            return std::string(start, end);
        };
        // fields: 0 = rom name, 1 = flags, 2 = address, 3 = value,
        //         4 = attribute, 5 = cheat name
        std::string sFlags = field(1, 2);
        addrRaw = field(2, 3);
        std::string sValue = field(3, 4);
        std::string sAttrib = field(4, 5);

        // numeric interpretation (arcade). for NES/SNES the caller keeps
        // addrRaw verbatim (Game Genie / PAR code) instead of using address.
        address = (unsigned int) strtoul(addrRaw.c_str(), nullptr, 16);
        flags = (unsigned int) strtoul(sFlags.c_str(), nullptr, 16);
        value = (unsigned int) strtoul(sValue.c_str(), nullptr, 16);
        attrib = (unsigned int) strtoul(sAttrib.c_str(), nullptr, 16);
        name = field(5, 6);
        // strip surrounding quotes from the name
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
            name = name.substr(1, name.size() - 2);
        }
        return true;
    }

    static bool loadMameFile(const std::string &path, const std::string &romName) {
        FILE *f = fopen(path.c_str(), "rt");
        if (!f) {
            return false;
        }

        char line[2048];
        bool found = false;
        PfbCheat *current = nullptr;
        bool menu = false;    // current cheat has no direct address on its first line
        int n = 0;            // option index being filled for the current cheat
        std::string gName = ":" + romName + ":";
        bool loaded = false;

        while (fgets(line, sizeof(line), f)) {
            std::string trimmed = trim(line);
            if (trimmed.empty()) {
                continue;
            }
            if (trimmed[0] == ';') {
                continue;
            }

            // match every entry line by prefix ":<romname>:"
            if (trimmed.compare(0, gName.size(), gName) != 0) {
                if (found) {
                    break;  // this rom's section ended
                }
                continue;   // not reached this rom's entries yet
            }
            if (trimmed.find("----:REASON") != std::string::npos) {
                break;  // end of the database file
            }
            found = true;
            loaded = true;

            // cheat entry line
            unsigned int flags = 0, address = 0, value = 0, attrib = 0;
            std::string entryName;
            std::string addrRaw;
            if (!parseMameEntry(line, flags, address, value, attrib, entryName, addrRaw)) {
                continue;  // malformed line
            }

            // NES/SNES: the FBNeo frontend keeps the whole address field as a
            // Game Genie / PAR code string (hex-looking 8-char codes are PAR,
            // "XXXX-XXXX" and 6/8-char codes are Game Genie), and the NES/SNES
            // core decodes them via the nes_add_cheat() hook. zero/empty
            // fields mean "menu only" entries and get no code.
            std::string genie;
            if (HW_GGENIE) {
                if (!addrRaw.empty() && addrRaw != "0" && addrRaw != "0000"
                    && addrRaw != "00000000") {
                    genie = addrRaw;
                }
            } else if (!isHexString(addrRaw)) {
                continue;  // arcade entry with a non-hex address: skip
            }

            if (flags & 0x00004800) {
                continue;  // unsupported cheat methods
            }

            if ((flags & 0x00008000) || ((flags & 0x00010000) && !menu)) {
                // linked line: adds an address group to the current option
                if (current && n >= 0 && n < (int) current->optionAddresses.size()) {
                    CheatAddr a;
                    a.cpu = 0;  // FBA derives the cpu from flags >> 24; the
                                // common databases use cpu 0 for arcade writes
                    a.address = address;
                    a.value = value;
                    a.genie = genie;
                    current->optionAddresses[n].push_back(a);
                }
                continue;
            }

            if (~flags & 0x00010000) {
                // ---- new cheat
                n = 0;
                menu = false;
                s_cheats.push_back(PfbCheat());
                current = &s_cheats.back();
                current->name = entryName.empty() ? "Cheat" : entryName;
                // option 0: Disabled (no address)
                // option 0: always "禁用" (Disabled, no address). entries from
                // the mame databases uniformly show 禁用/启用 (+ any extra
                // real options the file defines, e.g. "# 1..# N").
                current->optionNames.push_back(TEXT_CHEAT_DISABLED);
                current->optionAddresses.emplace_back();
                loaded = true;

                if (address || !genie.empty()) {
                    if (flags & 0x100) {
                        // "add options": generate "# 1..# N" options
                        unsigned int total = value + 1;
                        if (total > 0xff) {
                            total = 0xff;
                        }
                        for (unsigned int v = 0; v < total; v++) {
                            char tmp[32];
                            snprintf(tmp, sizeof(tmp), "# %d.", v + 1);
                            n++;
                            current->optionNames.push_back(tmp);
                            std::vector<CheatAddr> addrs;
                            CheatAddr a;
                            a.cpu = 0;
                            a.address = address;
                            a.value = v;
                            a.genie = genie;
                            addrs.push_back(a);
                            current->optionAddresses.push_back(addrs);
                        }
                    } else {
                        // main option: name = "启用" (Enable), address = this
                        // line (the file's entry name is the cheat name, so
                        // using it for the option just duplicates the title).
                        n++;
                        current->optionNames.push_back(TEXT_CHEAT_ENABLED);
                        std::vector<CheatAddr> addrs;
                        CheatAddr a;
                        a.cpu = 0;
                        a.address = address;
                        a.value = value;
                        a.genie = genie;
                        addrs.push_back(a);
                        current->optionAddresses.push_back(addrs);
                    }
                } else {
                    menu = true;  // options come on following :0x10000: lines
                }
                continue;
            }

            // flags & 0x10000 && menu -> new option of the current cheat
            if ((flags & 0x00010000) && menu && current) {
                n++;
                current->optionNames.push_back(
                    entryName.empty() ? "Option" : entryName);
                std::vector<CheatAddr> addrs;
                CheatAddr a;
                a.cpu = 0;
                a.address = address;
                a.value = value;
                a.genie = genie;
                addrs.push_back(a);
                current->optionAddresses.push_back(addrs);
                continue;
            }

            // anything else inside the section is ignored
        }

        fclose(f);
        return loaded;
    }

    // ---- core list ----------------------------------------------------------

    // rebuild the core pCheatInfo linked list from s_cheats (freed by the
    // core's CheatExit() on driver exit). All cheats start inactive.
    static void buildCoreList() {
        pCheatInfo = nullptr;
        CheatInfo *tail = nullptr;

        for (const auto &cheat : s_cheats) {
            auto *ci = (CheatInfo *) calloc(1, sizeof(CheatInfo));
            if (!ci) {
                continue;
            }
            ci->nType = 0;
            ci->nStatus = -1;   // inactive
            ci->nCurrent = 0;
            ci->nDefault = 0;

            strncpy(ci->szCheatName, cheat.name.c_str(), CHEAT_MAX_NAME - 1);
            ci->szCheatName[CHEAT_MAX_NAME - 1] = '\0';

            int nOption = (int) cheat.optionNames.size();
            if (nOption > CHEAT_MAX_OPTIONS) {
                nOption = CHEAT_MAX_OPTIONS;
            }
            for (int i = 0; i < nOption; i++) {
                auto *opt = (CheatOption *) calloc(1, sizeof(CheatOption));
                if (!opt) {
                    continue;
                }
                strncpy(opt->szOptionName, cheat.optionNames[i].c_str(), CHEAT_MAX_NAME - 1);
                opt->szOptionName[CHEAT_MAX_NAME - 1] = '\0';

                const auto &addrs = cheat.optionAddresses[i];
                int nAddr = (int) addrs.size();
                if (nAddr > CHEAT_MAX_ADDRESS) {
                    nAddr = CHEAT_MAX_ADDRESS;
                }
                for (int a = 0; a < nAddr; a++) {
                    if (!addrs[a].genie.empty()) {
                        // NES/SNES Game Genie / PAR code: hand the code to the
                        // core via szGenieCode. nAddress must be NON-ZERO for
                        // the core's per-address loop to visit this entry; the
                        // NES/SNES hook ignores the numeric address.
                        strncpy(opt->AddressInfo[a].szGenieCode, addrs[a].genie.c_str(),
                                sizeof(opt->AddressInfo[a].szGenieCode) - 1);
                        opt->AddressInfo[a].szGenieCode[sizeof(opt->AddressInfo[a].szGenieCode) - 1] = '\0';
                        opt->AddressInfo[a].nAddress = 0xffff;
                        opt->AddressInfo[a].nValue = (UINT32) addrs[a].value;
                        opt->AddressInfo[a].nMultiByte = 0;
                        opt->AddressInfo[a].nTotalByte = 1;
                        opt->AddressInfo[a].nMask = 0xFF;
                    } else {
                        opt->AddressInfo[a].nCPU = addrs[a].cpu;
                        opt->AddressInfo[a].nAddress = (INT32) addrs[a].address;
                        opt->AddressInfo[a].nValue = (UINT32) addrs[a].value;
                        opt->AddressInfo[a].nMultiByte = 0;
                        opt->AddressInfo[a].nTotalByte = 1;
                        opt->AddressInfo[a].nMask = 0xFF;
                    }
                }
                ci->pOption[i] = opt;
            }

            // append to the list
            if (tail) {
                ci->pPrevious = tail;
                tail->pNext = ci;
            } else {
                pCheatInfo = ci;
            }
            tail = ci;
        }
    }

    void load(const ss_api::Game &game, c2d::Io *io) {
        s_cheats.clear();

        std::string dir;
        if (io) {
            dir = io->getDataPath() + "cheats/";
        }
        std::string zipName = Utility::removeExt(game.path);

        // the mame databases key on the CORE DRIVER name, which for NES/SNES
        // carries a "nes_"/"snes_" prefix (e.g. ":nes_contra:", ":snes_xxx:")
        // while arcade drivers equal the zip name (":wof:"). the FBA ini
        // files use the driver name as well, so match both when they differ.
        std::string drvName = BurnDrvGetTextA(DRV_NAME);

        cheatLog("=== pfbneo cheat load (v3): %s (zip=%s, drv=%s) ===",
                 game.path.c_str(), zipName.c_str(), drvName.c_str());

        bool any = false;

        // 1) per-rom FBA ini FIRST (driver name first, zip name as fallback).
        //    when a per-rom ini with cheats exists it TAKES PRIORITY and the
        //    global mame databases are skipped entirely (no double entries).
        if (!dir.empty()) {
            if (loadIniFile(dir + drvName + ".ini")) {
                any = true;
                cheatLog("  ini cheat found (%s.ini), skipping mame databases",
                         drvName.c_str());
            } else if (zipName != drvName && loadIniFile(dir + zipName + ".ini")) {
                any = true;
                cheatLog("  ini cheat found (%s.ini), skipping mame databases",
                         zipName.c_str());
            } else {
                cheatLog("  no ini: %s%s.ini", dir.c_str(), drvName.c_str());
            }
        }

        // 2) global mame-style databases, only when there is no per-rom ini:
        //    arcade -> cheat.dat (+ wayder_cheat.dat), NES/FC -> cheatnes.dat,
        //    SNES -> cheatsnes.dat
        if (!any) {
            if (HW_GGENIE) {
                std::string db = HW_NES ? "cheatnes.dat" : "cheatsnes.dat";
                if (loadMameFile(dir + db, drvName)) {
                    any = true;
                } else {
                    cheatLog("  no mame db: %s%s", dir.c_str(), db.c_str());
                }
            } else {
                if (loadMameFile(dir + "cheat.dat", drvName)) {
                    any = true;
                } else {
                    cheatLog("  no mame db: %scheat.dat", dir.c_str());
                }
                if (loadMameFile(dir + "wayder_cheat.dat", drvName)) {
                    any = true;
                } else {
                    cheatLog("  no mame db: %swayder_cheat.dat", dir.c_str());
                }
            }
        }

        if (!any) {
            cheatLog("  no cheat file found for this game");
            return;
        }

        cheatLog("  %zu cheat(s) loaded", s_cheats.size());

        // the burner frontend normally flips this on; without it both
        // CheatUpdate() and CheatEnable() early-out and cheats never apply
        bCheatsAllowed = true;

        // rebuild the core list (all cheats start inactive; runtime-only)
        buildCoreList();
        if (!pCheatInfo) {
            cheatLog("  pCheatInfo is empty");
            return;
        }
        cheatLog("  pCheatInfo ready, %d entry(s)", (int) s_cheats.size());
    }

    void selectOption(int cheatIndex, int optionIndex) {
        if (cheatIndex < 0 || cheatIndex >= (int) s_cheats.size()) {
            return;
        }
        const auto &cheat = s_cheats[cheatIndex];
        if (optionIndex < 0 || optionIndex >= (int) cheat.optionNames.size()) {
            return;
        }

        s_cheats[cheatIndex].selected = optionIndex;
        CheatEnable(cheatIndex, optionIndex);
        cheatLog("cheat[%d] '%s' -> option[%d] '%s'",
                 cheatIndex, cheat.name.c_str(), optionIndex,
                 cheat.optionNames[optionIndex].c_str());
    }

    void reset() {
        s_cheats.clear();
        bCheatsAllowed = false;
    }

    // ---- dialog accessors ----------------------------------------------------

    int getCheatCount() {
        return (int) s_cheats.size();
    }

    const char *getCheatName(int cheatIndex) {
        if (cheatIndex < 0 || cheatIndex >= (int) s_cheats.size()) {
            return "";
        }
        return s_cheats[cheatIndex].name.c_str();
    }

    int getOptionCount(int cheatIndex) {
        if (cheatIndex < 0 || cheatIndex >= (int) s_cheats.size()) {
            return 0;
        }
        return (int) s_cheats[cheatIndex].optionNames.size();
    }

    const char *getOptionName(int cheatIndex, int optionIndex) {
        if (cheatIndex < 0 || cheatIndex >= (int) s_cheats.size()) {
            return "";
        }
        const auto &cheat = s_cheats[cheatIndex];
        if (optionIndex < 0 || optionIndex >= (int) cheat.optionNames.size()) {
            return "";
        }
        return cheat.optionNames[optionIndex].c_str();
    }

    int getSelectedOption(int cheatIndex) {
        if (cheatIndex < 0 || cheatIndex >= (int) s_cheats.size()) {
            return 0;
        }
        return s_cheats[cheatIndex].selected;
    }

} // namespace pfbneo_cheat
