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
#include <utility>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unordered_map>

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

    // where the current game's cheats came from (see CheatSource in the header)
    static CheatSource g_source = CheatSource::None;
    static std::string g_sourceRom;

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

    // per-rom parsing context used while scanning a mame-style database file.
    // two contexts (own rom + parent rom) are filled in ONE single pass so a
    // big database is never scanned twice when the own rom has no entry.
    struct MameCtx {
        std::vector<PfbCheat> cheats;   // cheats parsed in this section
        PfbCheat *current = nullptr;    // cheat currently being filled
        bool menu = false;              // current cheat takes options on 0x10000 lines
        int n = 0;                      // option index inside the current cheat
        bool loaded = false;            // at least one real cheat parsed
    };

    // parse one entry line into a section context. flag interpretation is the
    // FBA one documented in the header comment above.
    static void mameParseLine(MameCtx &c, const char *line) {
        unsigned int flags = 0, address = 0, value = 0, attrib = 0;
        std::string entryName;
        std::string addrRaw;
        if (!parseMameEntry(line, flags, address, value, attrib, entryName, addrRaw)) {
            return;  // malformed line
        }

        // NES/SNES: the FBNeo frontend keeps the whole address field as a
        // Game Genie / PAR code string (hex-looking 8-char codes are PAR,
        // "XXXX-XXXX" and 6/8-char codes are Game Genie), and the NES/SNES
        // core decodes them via the nes_add_cheat() hook. zero/empty fields
        // mean "menu only" entries and get no code.
        std::string genie;
        if (HW_GGENIE) {
            if (!addrRaw.empty() && addrRaw != "0" && addrRaw != "0000"
                && addrRaw != "00000000") {
                genie = addrRaw;
            }
        } else if (!isHexString(addrRaw)) {
            return;  // arcade entry with a non-hex address: skip
        }

        if (flags & 0x00004800) {
            return;  // unsupported cheat methods
        }

        if ((flags & 0x00008000) || ((flags & 0x00010000) && !c.menu)) {
            // linked line: adds an address group to the current option
            if (c.current && c.n >= 0 && c.n < (int) c.current->optionAddresses.size()) {
                CheatAddr a;
                a.cpu = 0;  // FBA derives the cpu from flags >> 24; the
                            // common databases use cpu 0 for arcade writes
                a.address = address;
                a.value = value;
                a.genie = genie;
                c.current->optionAddresses[c.n].push_back(a);
            }
            return;
        }

        if (~flags & 0x00010000) {
            // ---- new cheat (or merge with the previous same-address entry)

            // the FBA frontend treats every plain line as a new cheat, but many
            // databases (e.g. captcommjr1 weapon/character select) list the
            // options of ONE selectable cheat as consecutive lines that share
            // the SAME address with different values. merge those into a single
            // multi-option cheat (address stays identical, each value becomes
            // one option) - matching what the database author intended. lines
            // with the 0x100 "add options" flag are excluded: they are their
            // own auto-generated option series (e.g. puzznic 0x101/0x103).
            if (!c.cheats.empty() && c.current && address
                && !(flags & 0x00004800) && !(flags & 0x00000100)) {
                PfbCheat &prev = c.cheats.back();
                // prev must be a regular cheat (has a real address option):
                // a menu cheat only has option 0 (Disabled, empty) and takes
                // its options on :0x10000: lines instead.
                if (prev.optionAddresses.size() >= 2
                    && !prev.optionAddresses[1].empty()
                    && prev.optionAddresses[1][0].address == address
                    && !entryName.empty()) {
                    // the first option currently carries the generic "启用"
                    // label; in a merged selection list it should show the
                    // first line's own description (like the FBA frontend),
                    // e.g. 选关: 关卡 1 / 关卡 2 / ... instead of 启用/关卡 2
                    if (prev.optionNames.size() >= 2
                        && prev.optionNames[1] == TEXT_CHEAT_ENABLED) {
                        prev.optionNames[1] = prev.name;
                    }
                    c.n = (int) prev.optionNames.size();
                    prev.optionNames.push_back(entryName);
                    std::vector<CheatAddr> addrs;
                    CheatAddr a;
                    a.cpu = 0;
                    a.address = address;
                    a.value = value;
                    a.genie = genie;
                    addrs.push_back(a);
                    prev.optionAddresses.push_back(addrs);
                    c.current = &prev;
                    return;
                }
            }

            // ---- new cheat
            c.n = 0;
            c.menu = false;
            c.cheats.push_back(PfbCheat());
            c.current = &c.cheats.back();
            // keep the description as-is: an empty description must stay an
            // empty row in the dialog (no "Cheat" placeholder)
            c.current->name = entryName;

            // like the FBNeo frontend (src/burner/conc.cpp): a database entry
            // with an empty description OR with flags == 0x60000000 is a
            // HEADER / comment line, not a real cheat. it gets NO options, so
            // the dialog shows it as an unselectable (bold) title row; an
            // empty name keeps the row empty.
            if (entryName.empty() || flags == 0x60000000) {
                c.loaded = true;
                return;
            }

            // option 0: always "禁用" (Disabled, no address). entries from
            // the mame databases uniformly show 禁用/启用 (+ any extra
            // real options the file defines, e.g. "# 1..# N").
            c.current->optionNames.push_back(TEXT_CHEAT_DISABLED);
            c.current->optionAddresses.emplace_back();
            c.loaded = true;

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
                        c.n++;
                        c.current->optionNames.push_back(tmp);
                        std::vector<CheatAddr> addrs;
                        CheatAddr a;
                        a.cpu = 0;
                        a.address = address;
                        a.value = v;
                        a.genie = genie;
                        addrs.push_back(a);
                        c.current->optionAddresses.push_back(addrs);
                    }
                } else {
                    // main option: name = "启用" (Enable), address = this
                    // line (the file's entry name is the cheat name, so
                    // using it for the option just duplicates the title).
                    c.n++;
                    c.current->optionNames.push_back(TEXT_CHEAT_ENABLED);
                    std::vector<CheatAddr> addrs;
                    CheatAddr a;
                    a.cpu = 0;
                    a.address = address;
                    a.value = value;
                    a.genie = genie;
                    addrs.push_back(a);
                    c.current->optionAddresses.push_back(addrs);
                }
            } else {
                c.menu = true;  // options come on following :0x10000: lines
            }
            return;
        }

        // flags & 0x10000 && menu -> new option of the current cheat
        if ((flags & 0x00010000) && c.menu && c.current) {
            c.n++;
            c.current->optionNames.push_back(entryName.empty() ? "Option" : entryName);
            std::vector<CheatAddr> addrs;
            CheatAddr a;
            a.cpu = 0;
            a.address = address;
            a.value = value;
            a.genie = genie;
            addrs.push_back(a);
            c.current->optionAddresses.push_back(addrs);
        }
    }

    // parent-rom cheats parsed from a database, kept aside until the parent
    // .ini lookup (which has HIGHER priority than the parent dat) is done.
    static std::vector<PfbCheat> s_datParentCheats;

    // ---- mame database cache ----------------------------------------------
    //
    // the mame databases are big (cheat.dat is a multi-megabyte file with
    // thousands of rom sections) and would otherwise be rescanned from the
    // start on EVERY game load (up to the ----:REASON end marker for roms
    // without an entry). to keep game loading fast the file is read ONCE per
    // session into memory and a rom-name -> byte-range index is built; each
    // game load then parses only its own (and optionally the parent) section.
    // the cache is invalidated when the file mtime/size changes.

    struct DatSection {
        std::string rom;
        size_t start = 0;   // offset of the ":<rom>:" header line
    };

    struct DatIndex {
        std::string path;
        time_t mtime = 0;
        long size = -1;
        std::string data;               // whole file (a few MB, read once)
        std::vector<DatSection> sections;
        size_t fileEnd = 0;             // offset of "----:REASON" (or EOF)
        bool valid = false;
    };

    // one cache slot per database file: several .dat databases can live in the
// cheats directory at once (cheat.dat, wayder_cheat.dat, NES/SNES ones,
// custom ones), and each game load may need a different one.
static std::unordered_map<std::string, DatIndex> s_indexes;

static bool fileStamp(const std::string &path, time_t &mtime, long &size) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    mtime = st.st_mtime;
    size = st.st_size;
    return true;
}

// read the whole file and build the section index (light scan: only the
// ":<rom>:" prefix of each line is extracted, entries are NOT parsed).
static void buildIndex(const std::string &path) {
    DatIndex idx;
    idx.path = path;

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        s_indexes.erase(path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 64 * 1024 * 1024) {   // sanity cap
        fclose(f);
        s_indexes.erase(path);
        return;
    }
    idx.data.resize((size_t) sz);
    size_t rd = fread(idx.data.data(), 1, (size_t) sz, f);
    fclose(f);
    if (rd != (size_t) sz) {
        s_indexes.erase(path);
        return;
    }
    idx.fileEnd = idx.data.size();

    std::string current;
    size_t pos = 0;
    while (pos < idx.data.size()) {
        size_t nl = idx.data.find('\n', pos);
        if (nl == std::string::npos) {
            nl = idx.data.size();
        }
        size_t lineStart = pos;
        pos = nl + 1;
        if (idx.data[lineStart] == ';') {
            continue;
        }
        if (idx.data[lineStart] != ':') {
            // end-of-database marker ("----:REASON" is its own line).
            // NOTE: only check WITHIN the current line - a whole-file
            // find() here would scan to EOF for every comment/blank line
            // and make indexing quadratic (a freeze on big databases).
            if (nl - lineStart >= 11
                && idx.data.compare(lineStart, 11, "----:REASON") == 0) {
                idx.fileEnd = lineStart;   // end of the database
                break;
            }
            continue;
        }
        // entry line ":<rom>:...": a new section starts when the rom name
        // differs from the previous line's rom
        size_t c1 = idx.data.find(':', lineStart + 1);
        if (c1 == std::string::npos || c1 >= nl) {
            continue;
        }
        std::string rom = idx.data.substr(lineStart + 1, c1 - lineStart - 1);
        if (rom != current) {
            current = rom;
            DatSection s;
            s.rom = rom;
            s.start = lineStart;
            idx.sections.push_back(s);
        }
    }
    idx.valid = true;
    s_indexes[path] = std::move(idx);
}

static bool ensureIndex(const std::string &path) {
    time_t mtime = 0;
    long size = -1;
    if (!fileStamp(path, mtime, size)) {
        s_indexes.erase(path);
        return false;
    }
    auto it = s_indexes.find(path);
    if (it != s_indexes.end() && it->second.valid
        && it->second.mtime == mtime && it->second.size == size) {
        return true;   // cache still valid
    }
    buildIndex(path);
    it = s_indexes.find(path);
    if (it != s_indexes.end()) {
        it->second.mtime = mtime;
        it->second.size = size;
    }
    return it != s_indexes.end() && it->second.valid;
}

// sorted list of *.dat filenames in a directory, excluding the given names
static std::vector<std::string> datFilesInDir(const std::string &dir,
                                              const std::vector<std::string> &exclude) {
    std::vector<std::string> out;
    DIR *d = opendir(dir.c_str());
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
            bool skip = false;
            for (const auto &x : exclude) {
                if (x == name) {
                    skip = true;
                    break;
                }
            }
            if (!skip) out.push_back(name);
        }
        closedir(d);
        std::sort(out.begin(), out.end());
    }
    return out;
}

    // parse one rom's section (all lines in [start, end) belong to that rom).
    static bool parseSection(const DatIndex &idx, size_t start, size_t end,
                             std::vector<PfbCheat> &out) {
        MameCtx c;
        size_t pos = start;
        while (pos < end) {
            size_t nl = idx.data.find('\n', pos);
            if (nl == std::string::npos || nl >= end) {
                nl = end;
            }
            std::string line(idx.data, pos, nl - pos);
            pos = nl + 1;
            if (line.empty()) {
                continue;
            }
            if (line.back() == '\r') {
                line.pop_back();
            }
            if (line[0] == ';') {
                continue;
            }
            mameParseLine(c, line.c_str());
        }
        out = std::move(c.cheats);
        return c.loaded;
    }

    // load a mame-style .dat database using the in-memory section index.
    // own-rom cheats are committed into s_cheats immediately; parent-rom
    // cheats go into s_datParentCheats for the caller to commit only if the
    // parent .ini also misses.
    // returns: 0 = nothing, 1 = own rom section, 2 = parent rom section.
    static int loadMameFile(const std::string &path, const std::string &romName,
                            const std::string &parentName) {
        if (!ensureIndex(path)) {
            return 0;
        }
        const DatIndex &idx = s_indexes[path];

        std::vector<PfbCheat> own;
        std::vector<PfbCheat> par;
        bool ownLoaded = false;
        bool parLoaded = false;

        for (size_t i = 0; i < idx.sections.size(); i++) {
            const DatSection &s = idx.sections[i];
            if (s.rom != romName && (parentName.empty() || s.rom != parentName)) {
                continue;
            }
            size_t end = (i + 1 < idx.sections.size())
                             ? idx.sections[i + 1].start
                             : idx.fileEnd;
            if (s.rom == romName) {
                ownLoaded = parseSection(idx, s.start, end, own);
            } else {
                parLoaded = parseSection(idx, s.start, end, par);
            }
            if (ownLoaded && parLoaded) {
                break;
            }
        }

        // own rom section takes priority; parent section is only a fallback
        if (ownLoaded) {
            s_cheats.insert(s_cheats.end(), own.begin(), own.end());
            return 1;
        }
        if (parLoaded) {
            s_datParentCheats = std::move(par);
            return 2;
        }
        return 0;
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
        // NOTE: DRV_NAME / DRV_PARENT can legally be NULL (a parent/standalone
        // driver has no szParent), so never pass them straight into a
        // std::string - that would be std::string(NULL) -> strlen(NULL) crash.
        const char *pDrvName = BurnDrvGetTextA(DRV_NAME);
        std::string drvName = pDrvName ? pDrvName : "";

        // parent (clone of) driver: when the own rom has no cheat file the
        // same lookup is repeated against the parent driver (e.g. wofj -> wof)
        const char *pParentName = BurnDrvGetTextA(DRV_PARENT);
        std::string parentName = pParentName ? pParentName : "";
        if (parentName.empty() || parentName == drvName) {
            parentName.clear();
        }

        // source bookkeeping for the dialog title
        g_source = CheatSource::None;
        g_sourceRom.clear();
        s_datParentCheats.clear();

        // lookup order: own ini > own dat > parent ini > parent dat. the own
        // dat and the parent dat share ONE database file, so both sections are
        // scanned in a single pass (own section committed immediately; the
        // parent section is kept aside until the parent ini lookup, which has
        // HIGHER priority, has also been tried).
        if (!dir.empty() && loadIniFile(dir + drvName + ".ini")) {
            g_source = CheatSource::Ini;
            g_sourceRom = drvName;
        } else {
            int r = 0;
            std::vector<std::string> tried;   // fixed databases already looked at
            if (HW_GGENIE) {
                std::string db = HW_NES ? "cheatnes.dat" : "cheatsnes.dat";
                tried.push_back(db);
                r = loadMameFile(dir + db, drvName, parentName);
            } else {
                tried.push_back("cheat.dat");
                r = loadMameFile(dir + "cheat.dat", drvName, parentName);
                if (r == 0) {
                    tried.push_back("wayder_cheat.dat");
                    r = loadMameFile(dir + "wayder_cheat.dat", drvName, parentName);
                }
            }
            if (r == 0) {
                // fallback: the cheats directory may hold OTHER .dat cheat
                // databases (custom ones); try each in sorted order
                auto others = datFilesInDir(dir, tried);
                for (const auto &f : others) {
                    r = loadMameFile(dir + f, drvName, parentName);
                    if (r != 0) {
                        break;
                    }
                }
            }
            if (r == 1) {
                g_source = CheatSource::Dat;
                g_sourceRom = drvName;
            } else if (r == 2) {
                // parent dat hit - but the parent ini has higher priority
                if (!dir.empty() && !parentName.empty()
                    && loadIniFile(dir + parentName + ".ini")) {
                    g_source = CheatSource::ParentIni;
                    g_sourceRom = parentName;
                    s_datParentCheats.clear();
                } else {
                    s_cheats.insert(s_cheats.end(), s_datParentCheats.begin(),
                                    s_datParentCheats.end());
                    g_source = CheatSource::ParentDat;
                    g_sourceRom = parentName;
                }
            } else if (!dir.empty() && !parentName.empty()
                       && loadIniFile(dir + parentName + ".ini")) {
                g_source = CheatSource::ParentIni;
                g_sourceRom = parentName;
            }
        }

        if (g_source == CheatSource::None) {
            return;
        }

        // the burner frontend normally flips this on; without it both
        // CheatUpdate() and CheatEnable() early-out and cheats never apply
        bCheatsAllowed = true;

        // rebuild the core list (all cheats start inactive; runtime-only)
        buildCoreList();
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
    }

    void reset() {
        s_cheats.clear();
        bCheatsAllowed = false;
    }

    void preload() {
        // index EVERY cheat database .dat file found in the cheats directory
        // (multiple databases can coexist: cheat.dat, wayder_cheat.dat, NES/
        // SNES ones, custom ones) so the first game load never pays the file
        // read+index scan; the index map keeps one entry per file.
        std::string base = "./";
        if (c2d_renderer && c2d_renderer->getIo()) {
            base = c2d_renderer->getIo()->getDataPath();
            if (base.empty()) base = "./";
        }
        auto files = datFilesInDir(base + "cheats/", {});
        for (const auto &f : files) {
            ensureIndex(base + "cheats/" + f);
        }
    }

    // ---- dialog accessors ----------------------------------------------------

    CheatSource getSource() {
        return g_source;
    }

    const std::string &getSourceRom() {
        return g_sourceRom;
    }

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
