//
// pfbneo move list module implementation (see pfbneo_movelist.h).
//

// NOTE on include order: "skeleton/pemu.h" MUST come first, same rule as
// pfbneo_keybind.cpp / pfbneo_dip.cpp / pfbneo_memcard.cpp / pfbneo_ips.cpp
// (it pulls in ss_api.h, cross2d/c2d.h, pemu_config.h, ui_text_def.h AND the
// "using namespace ss_api").

#include "skeleton/pemu.h"
#include "pfbneo_movelist.h"
#include "pfbneo_utility.h"
#include "burner.h"
#include "burnint.h"
#include "skeleton/ui_text_def.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/stat.h>

namespace pfbneo_movelist {

struct Move {
    std::string name;
    std::vector<std::string> commands;
};

// parsed sections of the current command.dat, keyed by driver name
// (lowercased); the whole file is parsed once and reused across menu opens
// (invalidated when the file mtime/size changes)
static std::unordered_map<std::string, std::vector<Move>> s_cache;
static std::string s_cachePath;
static time_t s_cacheMtime = 0;
static long s_cacheSize = -1;

// current game's move list (copied from the cache on game config create)
static std::vector<Move> s_moves;
static bool s_has = false;
static std::string s_romName;

static std::string dataPath() {
    if (c2d_renderer && c2d_renderer->getIo()) {
        std::string p = c2d_renderer->getIo()->getDataPath();
        return p.empty() ? "./" : p;
    }
    return "./";
}

// command.dat location: the win32 layout is <data path>/commands/command.dat
// but many setups drop it straight into the data path root; accept both.
static std::string findCommandDat() {
    std::string base = dataPath();
    std::string sub = base + "commands/command.dat";
    struct stat st;
    if (stat(sub.c_str(), &st) == 0) {
        return sub;
    }
    return base + "command.dat";
}

static void trim(std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        s.clear();
        return;
    }
    size_t e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);
}

// parse the WHOLE command.dat once into s_cache: for every "$info=" section,
// store the move groups under each listed driver. two group styles are
// supported (they can even mix inside one section):
//   - "$cmd" blocks: "$cmd" followed by a "[ 标题 ]" line and then the
//     command text lines (the format used by the chinese command lists),
//   - "- " lines: a "- 招式名" line followed by its command text (the format
//     used by progettosnaps command.dat).
// free text before the first group of a section (game title / intro notes)
// is skipped, so it never shows up as a bogus move row.
static void buildCache() {
    s_cache.clear();
    s_cachePath.clear();
    s_cacheMtime = 0;
    s_cacheSize = -1;

    struct stat st;
    if (stat(findCommandDat().c_str(), &st) != 0) return;
    s_cachePath = findCommandDat();
    s_cacheMtime = st.st_mtime;
    s_cacheSize = st.st_size;

    FILE *fp = fopen(s_cachePath.c_str(), "rt");
    if (!fp) return;

    std::vector<std::string> drivers;   // drivers listed in the current $info=
    std::vector<Move> sectionMoves;     // move groups of the current section
    Move cur;                           // current group being filled
    bool curPending = false;            // "$cmd" seen, title line not yet read
    bool curTitleSet = false;

    auto closeGroup = [&]() {
        // a group without any command lines is useless in the accordion view
        if (curTitleSet && !cur.commands.empty()) {
            sectionMoves.push_back(cur);
        }
        cur = Move();
        curPending = false;
        curTitleSet = false;
    };
    auto commitSection = [&]() {
        closeGroup();
        if (!sectionMoves.empty()) {
            for (auto &d : drivers) {
                // first section wins for a driver (matches the win32 frontend
                // which stops at the first $info= match)
                if (!s_cache.count(d)) {
                    s_cache[d] = sectionMoves;
                }
            }
        }
        drivers.clear();
        sectionMoves.clear();
    };

    char line[8192];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        if (strncmp(p, "\xef\xbb\xbf", 3) == 0) p += 3;   // UTF-8 BOM

        if (strncmp(p, "$info=", 6) == 0) {
            commitSection();
            // tokenize the driver list (comma separated)
            std::string list(p + 6);
            size_t pos = 0;
            while (pos <= list.size()) {
                size_t next = list.find(',', pos);
                if (next == std::string::npos) next = list.size();
                std::string tok = list.substr(pos, next - pos);
                trim(tok);
                std::transform(tok.begin(), tok.end(), tok.begin(), ::tolower);
                if (!tok.empty()) drivers.push_back(tok);
                if (next == list.size()) break;
                pos = next + 1;
            }
        } else if (strncmp(p, "$end", 4) == 0) {
            commitSection();
        } else if (!drivers.empty()) {   // inside a section
            if (strncmp(p, "$cmd", 4) == 0) {
                closeGroup();            // "$cmd" starts a new group
                curPending = true;
                continue;
            }
            if (p[0] == '$') continue;
            std::string s(p);
            trim(s);
            if (s.empty()) continue;
            // separator lines (pure dashes / box drawing, e.g. "──────") are
            // visual noise in the accordion; matched byte-wise for the UTF-8
            // sequences of ─ (E2 94 80), — (E2 80 94) and · (C2 B7)
            bool sep = true;
            for (size_t i = 0; i < s.size();) {
                unsigned char c = (unsigned char) s[i];
                if (c == '-' || c == '=') {
                    i++;
                } else if (i + 2 < s.size() && c == 0xE2
                           && (unsigned char) s[i + 1] == 0x94
                           && (unsigned char) s[i + 2] == 0x80) {
                    i += 3;
                } else if (i + 2 < s.size() && c == 0xE2
                           && (unsigned char) s[i + 1] == 0x80
                           && (unsigned char) s[i + 2] == 0x94) {
                    i += 3;
                } else if (i + 1 < s.size() && c == 0xC2
                           && (unsigned char) s[i + 1] == 0xB7) {
                    i += 2;
                } else {
                    sep = false;
                    break;
                }
            }
            if (sep) continue;

            if (s.compare(0, 2, "- ") == 0) {
                closeGroup();            // "- name" starts a new group
                cur.name = s.substr(2);
                trim(cur.name);
                curTitleSet = true;
            } else if (curPending) {
                // first line of a "$cmd" block: "[ 标题 ]" (brackets stripped)
                // or the raw text
                curPending = false;
                curTitleSet = true;
                if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
                    cur.name = s.substr(1, s.size() - 2);
                    trim(cur.name);
                } else {
                    cur.name = s;
                }
            } else if (curTitleSet) {
                cur.commands.push_back(s);
            }
            // free text before the first group of the section: skipped
        }
    }
    commitSection();

    fclose(fp);
}

static void ensureCache() {
    std::string path = findCommandDat();
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (!s_cachePath.empty() || !s_cache.empty()) {
            s_cache.clear();
            s_cachePath.clear();
            s_cacheMtime = 0;
            s_cacheSize = -1;
        }
        return;
    }
    if (s_cachePath == path && s_cacheMtime == st.st_mtime
        && s_cacheSize == st.st_size) {
        return;   // cache still valid
    }
    buildCache();
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------
void init() {
    s_moves.clear();
    s_has = false;
    s_romName.clear();
    s_cache.clear();
    s_cachePath.clear();
    s_cacheMtime = 0;
    s_cacheSize = -1;
}

void preload() {
    // force a rebuild so a command.dat that was added/changed after boot is
    // picked up; the parse itself is cheap (a few ms for a 10MB file)
    s_cache.clear();
    s_cachePath.clear();
    s_cacheMtime = 0;
    s_cacheSize = -1;
    ensureCache();
}

void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig) {
    (void) game;
    (void) gameConfig;
    s_moves.clear();
    s_has = false;
    s_romName.clear();

    PFBNEOUtility::setDriverActive(game);
    if (nBurnDrvActive >= nBurnDrvCount) return;
    const char *name = BurnDrvGetTextA(DRV_NAME);
    if (!name || !name[0]) return;
    s_romName = name;

    // the whole file is parsed at most once (per mtime/size); the per-game
    // lookup below is O(1), so opening the menu stays fast
    ensureCache();

    std::string d = s_romName;
    std::transform(d.begin(), d.end(), d.begin(), ::tolower);
    auto it = s_cache.find(d);
    if (it == s_cache.end()) {
        // home consoles: "nes_smb" also matches a plain "smb" entry
        size_t us = d.find('_');
        if (us != std::string::npos) {
            it = s_cache.find(d.substr(us + 1));
        }
    }
    if (it != s_cache.end()) {
        s_moves = it->second;
        s_has = !s_moves.empty();
    }
}

void onGameStop() {
    s_moves.clear();
    s_has = false;
    s_romName.clear();
}

// ---------------------------------------------------------------------------
// feature detection
// ---------------------------------------------------------------------------
bool hasMovelist() {
    return s_has;
}

const char *getRomName() {
    return s_romName.c_str();
}

// ---------------------------------------------------------------------------
// move groups
// ---------------------------------------------------------------------------
int getMoveCount() {
    return (int) s_moves.size();
}

const char *getMoveName(int idx) {
    if (idx < 0 || idx >= (int) s_moves.size()) return "";
    return s_moves[idx].name.c_str();
}

int getCommandCount(int idx) {
    if (idx < 0 || idx >= (int) s_moves.size()) return 0;
    return (int) s_moves[idx].commands.size();
}

const char *getCommand(int idx, int cmdIdx) {
    if (idx < 0 || idx >= (int) s_moves.size()) return "";
    if (cmdIdx < 0 || cmdIdx >= (int) s_moves[idx].commands.size()) return "";
    return s_moves[idx].commands[cmdIdx].c_str();
}

} // namespace pfbneo_movelist