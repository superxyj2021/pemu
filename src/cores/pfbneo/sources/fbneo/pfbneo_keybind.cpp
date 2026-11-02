//
// pfbneo key settings (按键设置) support module implementation.
//
// See pfbneo_keybind.h for the design notes.
//
// Turbo rate levels (frames are 1/60s in pfbneo):
//   index | period | on | off | presses/sec (approx)
//      0  |   16   |  2 | 14  | ~4Hz   (slowest)
//      1  |   12   |  2 | 10  | ~5Hz
//      2  |    8   |  2 |  6  | ~8Hz
//      3  |    6   |  2 |  4  | ~10Hz
//      4  |    4   |  2 |  2  | ~15Hz  (default)
//      5  |    2   |  1 |  1  | ~30Hz  (fastest)
//

// NOTE on include order:
//  "skeleton/pemu.h" MUST come first: it is the only header that pulls in
//  ss_api.h, cross2d/c2d.h, skeleton/pemu_config.h, skeleton/ui_text_def.h
//  AND the "using namespace ss_api" that pfbneo_utility.h relies on
//  (pfbneo_utility.h uses the unqualified type "Game").

#include "skeleton/pemu.h"
#include "pfbneo_keybind.h"
#include "pfbneo_utility.h"
#include "burner.h"
#include "retro_input.h"
#include "retro_input_wrapper.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace c2d;

// globals exported from the core input layer (burner.h / retro_input*), must
// stay at global scope (NOT inside the namespace below)
extern struct GameInp *GameInp;
extern UINT32 nGameInpCount;
extern UINT32 nMacroCount;

namespace pfbneo_keybind {

    // 6 rate levels: [period_frames, on_frames]
    static const int s_ratePeriod[6] = { 16, 12, 8, 6, 4, 2 };
    static const int s_rateOnFrames[6] = { 2, 2, 2, 2, 2, 1 };
    static const int s_rateDefaultIndex = 2; // 8Hz

    static const char *rateName(int index) {
        static const char *names[6] = { TEXT_KEYS_RATE_SLOW, TEXT_KEYS_RATE_MEDIUM,
                                        TEXT_KEYS_RATE_FAST, TEXT_KEYS_RATE_FASTER,
                                        TEXT_KEYS_RATE_VERY_FAST, TEXT_KEYS_RATE_FASTEST };
        if (index < 0 || index > 5) return "?";
        return names[index];
    }

    // one row of the key dialog: a player driver input or a combo macro
    struct Row {
        int inputIndex = -1;    // driver input index, or -1 for a macro row
        int macroIndex = -1;    // macro index, or -1 for a driver input row
        int port = 0;           // the input's player port (0 = P1)
        int defaultKey = -1;    // physical key of the default JOY binding
        // (resolved at build time, while the probe/running GameInp is alive;
        // -1 for macro rows and rows without a default)
        std::string name;       // Chinese display name (P1 prefix stripped)
        bool turboAllowed = false;
    };

    // runtime entry indexed by nCode
    struct Entry {
        c2d::config::Option *bindOpt = nullptr;   // physical key id, -1 = unbound
        c2d::config::Option *turboOpt = nullptr;  // turbo on/off (null when not allowed)
        int port = 0;
        int defaultKey = -1;                      // default JOY binding (P1 rows)
        unsigned int counter = 0;                 // turbo hold counter
    };

    static c2d::config::Config *s_gameConfig = nullptr;
    static c2d::config::Option *s_rateOptCached = nullptr;
    static int s_rateIndex = s_rateDefaultIndex;
    static std::vector<Row> s_rows;
    static std::vector<c2d::config::Option *> s_bindOpts;   // per row
    static std::vector<c2d::config::Option *> s_turboOpts;  // per row (null when not allowed)
    static int s_driverRowCount = 0;                        // rows before the macro rows
    static Entry s_entries[MAX_KEYBINDS];

    // ---------------------------------------------------------------------------
    // helpers
    // ---------------------------------------------------------------------------
    static bool ciEq(const char *a, const char *b) {
        while (*a && *b) {
            if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) return false;
            a++;
            b++;
        }
        return *a == *b;
    }

    static bool ciStartsWith(const char *s, const char *prefix) {
        while (*prefix) {
            if (tolower((unsigned char) *s) != tolower((unsigned char) *prefix)) return false;
            s++;
            prefix++;
        }
        return true;
    }

    static bool ciContains(const char *s, const char *needle) {
        if (!*needle) return true;
        for (; *s; s++) {
            const char *a = s;
            const char *b = needle;
            while (*b && tolower((unsigned char) *a) == tolower((unsigned char) *b)) {
                a++;
                b++;
            }
            if (!*b) return true;
        }
        return false;
    }

    // is the description a direction (up/down/left/right as the first word)?
    // covers "Up", "Up (left)", "Right (Cocktail)", ... - these get no turbo.
    static bool isDirectionName(const char *desc) {
        if (ciStartsWith(desc, "up ")) return true;
        if (ciStartsWith(desc, "down ")) return true;
        if (ciStartsWith(desc, "left ")) return true;
        if (ciStartsWith(desc, "right ")) return true;
        return ciEq(desc, "up") || ciEq(desc, "down") || ciEq(desc, "left") || ciEq(desc, "right");
    }

    // turbo is not offered for directions and coin/select/start
    static bool turboAllowedFor(const char *desc) {
        if (ciContains(desc, "coin") || ciContains(desc, "start") || ciContains(desc, "select")) {
            return false;
        }
        if (isDirectionName(desc)) {
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Chinese display-name translation table (Chinese (Simplified)_inputs.glt)
    //
    // Works exactly like the game-list name table: a GBK "key\tvalue" file in
    // the localisation directory, bundled inside the nro romfs with an sd-side
    // override (sd wins). Keys are the logical key descriptions with the
    // "P1 "/"P2 " prefix stripped; when an entry is missing the original
    // English text is used as-is.
    // ---------------------------------------------------------------------------
    static std::unordered_map<std::string, std::string> s_translations;

    static bool loadTranslationTable() {
        static bool loaded = false;
        if (loaded) {
            return true;
        }
        loaded = true;

        if (!c2d_renderer) return false;
        c2d::Io *io = c2d_renderer->getIo();
        if (!io) return false;

        static const char *FILE_NAME = "localisation/Chinese (Simplified)_inputs.glt";
        std::string path = io->getDataPath() + FILE_NAME;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            path = io->getRomFsPath() + FILE_NAME;
            file.open(path, std::ios::binary);
        }
        if (!file.is_open()) {
            printf("pfbneo_keybind: no input translation table (%s), using English names\n", FILE_NAME);
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // 表文件为 UTF-8（内置副本）；为兼容旧的 GBK SD 副本，非 UTF-8 时按 GBK 转换
        std::string utf8 = PFBNEOUtility::isUtf8(content) ? content : PFBNEOUtility::gbkToUtf8(content);

        s_translations.clear();
        std::stringstream ss(utf8);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty() || line.compare(0, 2, "//") == 0) {
                continue;
            }
            auto tab = line.find('\t');
            if (tab == std::string::npos) {
                continue;
            }
            std::string key = line.substr(0, tab);
            std::string val = line.substr(tab + 1);
            size_t b = key.find_first_not_of(" \t");
            size_t e = key.find_last_not_of(" \t");
            if (b == std::string::npos) continue;
            key = key.substr(b, e - b + 1);
            if (key.empty() || val.empty()) continue;
            s_translations[key] = val;
        }

        printf("pfbneo_keybind: input translation table: %s, entries: %zu\n",
               path.c_str(), s_translations.size());
        return !s_translations.empty();
    }

    std::string getDisplayName(const std::string &desc) {
        // lazily load the translation table (sd copy wins, romfs fallback)
        if (!loadTranslationTable()) {
            return desc;
        }

        std::string s = desc;
        // strip the "P1 "/"P2 " ... prefix (case-insensitive), as the game
        // driver groups inputs per player
        if (s.size() > 2 && (s[0] == 'P' || s[0] == 'p') &&
            s[1] >= '1' && s[1] <= '6' && s[2] == ' ') {
            s = s.substr(3);
        }

        auto it = s_translations.find(s);
        if (it != s_translations.end()) {
            return it->second;
        }
        return s;   // not translated: keep the original English text
    }

    // ---------------------------------------------------------------------------
    // row enumeration
    // ---------------------------------------------------------------------------
    // Replicates the player-input filter of GameInpAutoOne()/AnalyzeGameLayout():
    //   - "P1 xxx"/"P2 xxx" (case-insensitive) in szInfo or szName
    //   - digital only (DIP switches / constants and analog axes are skipped)
    //   - board functions (service, tilt, reset, diag, volume, hopper) and
    //     mahjong/mouse controls are not player buttons
    static bool isPlayerInput(const struct BurnInputInfo &bii) {
        const char *szi = bii.szInfo ? bii.szInfo : "";
        const char *szn = bii.szName ? bii.szName : "";
        bool bPlayerInInfo = (toupper((unsigned char) szi[0]) == 'P' && szi[1] >= '1' && szi[1] <= (MAX_PLAYERS + '0'));
        bool bPlayerInName = (szn[0] == 'P' && szn[1] >= '1' && szn[1] <= (MAX_PLAYERS + '0'));
        if (!bPlayerInInfo && !bPlayerInName) {
            return false;
        }
        if (bii.nType & BIT_GROUP_CONSTANT) return false;   // DIP / constant
        if (bii.nType & BIT_GROUP_ANALOG) return false;     // joystick axes, not buttons
        const std::string both = std::string(szi) + " " + szn;
        if (ciContains(both.c_str(), "service") || ciContains(both.c_str(), "tilt") ||
            ciContains(both.c_str(), "reset") || ciContains(both.c_str(), "diag") ||
            ciContains(both.c_str(), "volume") || ciContains(both.c_str(), "hopper")) {
            return false;
        }
        if (ciStartsWith(both.c_str(), "mah ") || ciContains(both.c_str(), " mah ") ||
            ciContains(both.c_str(), "mouse")) return false;
        return true;
    }

    static int playerOf(const struct BurnInputInfo &bii) {
        const char *szi = bii.szInfo ? bii.szInfo : "";
        const char *szn = bii.szName ? bii.szName : "";
        if (szn[0] == 'P' && szn[1] >= '1' && szn[1] <= '6') return szn[1] - '1';
        if (szi[0] == 'P' && szi[1] >= '1' && szi[1] <= '6') return szi[1] - '1';
        return 0;
    }

    static std::string descriptionOf(const struct BurnInputInfo &bii) {
        // prefer the human-readable name ("P1 Attack") over the mapping key
        // ("p1 fire 1") when the driver provides one
        if (bii.szName && bii.szName[0]) return bii.szName;
        return bii.szInfo ? bii.szInfo : "";
    }

    static int resolveDefaultKey(const struct GameInp &pgi) {
        if (pgi.nInput != GIT_SWITCH) return -1;
        unsigned int nCode = pgi.Input.Switch.nCode;
        if (nCode >= MAX_KEYBINDS) return -1;
        unsigned int btn = PfbneoGetLogicalButton(nCode);   // c2d logical button
        if (!c2d_renderer) return -1;
        auto *input = c2d_renderer->getInput();
        auto *player = input ? input->getPlayer(0) : nullptr;
        if (!player) return -1;
        for (const auto &m : player->mapping) {
            if ((unsigned int) m.button == btn) {
                return m.value;
            }
        }
        return -1;
    }

    // build the row list from the currently allocated GameInp[] array (the
    // running game or a probe result). the walk order (player-1 driver inputs
    // in driver order, then macros) is the same order used to create the
    // config options. only PLAYER 1 inputs are shown (shared for all players).
    static void buildRowsFromGameInp() {
        s_rows.clear();
        if (!GameInp) return;

        struct BurnInputInfo bii;
        for (UINT32 i = 0; i < nGameInpCount; i++) {
            memset(&bii, 0, sizeof(bii));
            BurnDrvGetInputInfo(&bii, i);
            if (!isPlayerInput(bii)) continue;
            Row row;
            row.inputIndex = (int) i;
            row.port = playerOf(bii);
            if (row.port != 0) continue;   // one player only
            row.defaultKey = resolveDefaultKey(*(GameInp + i));
            row.name = getDisplayName(descriptionOf(bii));
            row.turboAllowed = turboAllowedFor(descriptionOf(bii).c_str());
            s_rows.push_back(row);
        }
        s_driverRowCount = (int) s_rows.size();

        // combo macros appended by AnalyzeGameLayout (they all allow turbo); only
        // player-1 macros are rows (the P2..P6 sets are duplicates)
        for (UINT32 m = 0; m < nMacroCount; m++) {
            struct GameInp *pgi = GameInp + nGameInpCount + m;
            if (!pgi || pgi->nInput != GIT_MACRO_AUTO) continue;
            Row row;
            row.macroIndex = (int) m;
            row.port = 0;
            const char *szName = pgi->Macro.szName[0] ? pgi->Macro.szName : "Macro";
            if (szName[0] == 'P' && szName[1] >= '1' && szName[1] <= '6') {
                row.port = szName[1] - '1';
            }
            if (row.port != 0) continue;   // one player only
            row.name = getDisplayName(szName);
            row.turboAllowed = true;
            s_rows.push_back(row);
        }
        // order the macro rows by display name length, then lexically
        // (e.g. 组合12, 组合13, 组合23, 组合123) regardless of the creation
        // order of the different combo types
        std::sort(s_rows.begin() + s_driverRowCount, s_rows.end(),
                  [](const Row &a, const Row &b) {
                      if (a.name.size() != b.name.size()) {
                          return a.name.size() < b.name.size();
                      }
                      return a.name < b.name;
                  });
    }

    // ---------------------------------------------------------------------------
    // runtime entry resolution (row -> nCode)
    // ---------------------------------------------------------------------------
    static void resolveEntries() {
        for (int i = 0; i < MAX_KEYBINDS; i++) {
            s_entries[i] = Entry();
        }
        if (!GameInp) {
            return;
        }

        // driver input rows (same filtered walk as buildRowsFromGameInp)
        int row = 0;
        struct BurnInputInfo bii;
        for (UINT32 i = 0; i < nGameInpCount && row < (int) s_rows.size(); i++) {
            memset(&bii, 0, sizeof(bii));
            BurnDrvGetInputInfo(&bii, i);
            if (!isPlayerInput(bii)) continue;
            if (playerOf(bii) != 0) continue;   // one player only
            if (row >= s_driverRowCount) break;
            struct GameInp *pgi = GameInp + i;
            unsigned int nCode = 0;
            if (pgi->nInput == GIT_SWITCH) {
                nCode = pgi->Input.Switch.nCode;
            }
            if (nCode < MAX_KEYBINDS && pgi->nInput == GIT_SWITCH) {
                s_entries[nCode].bindOpt = s_bindOpts[row];
                s_entries[nCode].turboOpt = s_turboOpts[row];
                s_entries[nCode].port = s_rows[row].port;
                s_entries[nCode].defaultKey = s_rows[row].defaultKey;
            }
            row++;
        }

        // macro rows: iterate the (sorted) macro rows and map each back to its
        // GameInp entry via the stored macro index
        for (int row2 = s_driverRowCount; row2 < (int) s_rows.size(); row2++) {
            if (s_rows[row2].macroIndex < 0) continue;
            int m = s_rows[row2].macroIndex;
            struct GameInp *pgi = GameInp + nGameInpCount + m;
            if (!pgi || pgi->nInput != GIT_MACRO_AUTO) continue;
            unsigned int nCode = pgi->Macro.Switch.nCode;
            if (nCode < MAX_KEYBINDS) {
                s_entries[nCode].bindOpt = s_bindOpts[row2];
                s_entries[nCode].turboOpt = s_turboOpts[row2];
                s_entries[nCode].port = s_rows[row2].port;
                s_entries[nCode].defaultKey = s_rows[row2].defaultKey;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // lifecycle
    // ---------------------------------------------------------------------------
    void init() {
        for (int i = 0; i < MAX_KEYBINDS; i++) {
            s_entries[i] = Entry();
        }
        s_rateIndex = s_rateDefaultIndex;
    }

    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig) {
        s_gameConfig = gameConfig;
        s_rows.clear();
        s_bindOpts.clear();
        s_turboOpts.clear();
        s_driverRowCount = 0;

        // make sure the requested driver is active (same helper the emulator
        // uses on load; a no-op when it already is)
        int oldActive = nBurnDrvActive;
        int requested = PFBNEOUtility::setDriverActive(game);
        bool sameDriver = (oldActive == requested && requested >= 0 && requested < nBurnDrvCount);

        if (sameDriver && PfbneoInputInitialized()) {
            // the game is running (in-game menu): GameInp is already mapped,
            // do NOT re-run the probe (that would clobber the running mapping)
            buildRowsFromGameInp();
        } else {
            // rom menu (game not started): run the real mapping once exactly
            // like the emulator does (see PfbneoProbeInputs in retro_input.cpp),
            // then free the probe allocation
            bIsNeogeoCartGame = ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_SNK_NEOGEO);
            PfbneoProbeInputs();
            buildRowsFromGameInp();
            PfbneoProbeFree();
        }

        // create the per-rom "KEYBIND" config group
        c2d::config::Group keyGroup("KEYBIND", KEYBIND_GROUP_ID, TEXT_MENU_KEYS);
        std::vector<std::string> rates;
        for (int i = 0; i < 6; i++) {
            rates.emplace_back(rateName(i));
        }
        keyGroup.addOption({"KEYBIND_RATE", rates, s_rateDefaultIndex, KEYBIND_OPT_RATE, TEXT_KEYS_RATE_HINT, TEXT_KEYS_RATE});

        for (size_t r = 0; r < s_rows.size(); r++) {
            const std::string key = std::to_string((long long) r);
            keyGroup.addOption({"KEYBIND_BIND_" + key, -1, KEYBIND_OPT_BIND + (int) r * 2, "", s_rows[r].name});
            if (s_rows[r].turboAllowed) {
                std::vector<std::string> onOff = {TEXT_KEYS_TURBO_OFF, TEXT_KEYS_TURBO_ON};
                keyGroup.addOption({"KEYBIND_TURBO_" + key, onOff, 0, KEYBIND_OPT_TURBO + (int) r * 2, "", s_rows[r].name});
            }
        }
        gameConfig->addGroup(keyGroup);

        // cache the option pointers for the per-frame lookups (the config
        // object is recreated per game / per menu open, so re-cache every load)
        s_rateOptCached = gameConfig->getOption(KEYBIND_OPT_RATE);
        s_bindOpts.clear();
        s_turboOpts.clear();
        for (size_t r = 0; r < s_rows.size(); r++) {
            s_bindOpts.push_back(gameConfig->getOption(KEYBIND_OPT_BIND + (int) r * 2));
            c2d::config::Option *turbo = s_rows[r].turboAllowed
                                         ? gameConfig->getOption(KEYBIND_OPT_TURBO + (int) r * 2)
                                         : nullptr;
            s_turboOpts.push_back(turbo);
        }

        // resolve the nCode table right away when the mapping already exists
        // (in-game menu case), otherwise it is resolved on game start
        resolveEntries();
    }

    void onGameStart() {
        resolveEntries();
    }

    void onGameStop() {
        s_gameConfig = nullptr;
        s_rateOptCached = nullptr;
        s_rows.clear();
        s_bindOpts.clear();
        s_turboOpts.clear();
        s_driverRowCount = 0;
        for (int i = 0; i < MAX_KEYBINDS; i++) {
            s_entries[i] = Entry();
        }
        s_rateIndex = s_rateDefaultIndex;
    }

    // ---------------------------------------------------------------------------
    // per-frame hooks
    // ---------------------------------------------------------------------------
    void tick() {
        if (!c2d_renderer) return;
        auto *input = c2d_renderer->getInput();
        if (!input) return;

        // refresh the shared rate from the config (applies immediately)
        if (s_rateOptCached) {
            int idx = s_rateOptCached->getArrayIndex();
            if (idx >= 0 && idx <= 5) {
                s_rateIndex = idx;
            }
        }

        // advance per-input turbo hold counters from the raw bound key state
        for (int nCode = 0; nCode < MAX_KEYBINDS; nCode++) {
            Entry &e = s_entries[nCode];
            if (!e.bindOpt || !e.turboOpt) continue;
            int key = e.bindOpt->getInteger();
            if (key == KEYBIND_BIND_CLEARED) {   // explicitly cleared: no polling
                e.counter = 0;
                continue;
            }
            if (key < 0) key = e.defaultKey;   // effective binding
            if (key < 0 || !e.turboOpt->getArrayIndex()) {
                e.counter = 0;
                continue;
            }
            auto *player = input->getPlayer(e.port);
            if (!player || !player->enabled || !player->data) {
                e.counter = 0;
                continue;
            }
            if (input->getButtonState(*player, key)) {
                if (e.counter < 0x7FFFFFFFu) e.counter++;
            } else {
                e.counter = 0;
            }
        }
    }

    int query(int nCode) {
        if (nCode < 0 || nCode >= MAX_KEYBINDS) return -1;
        Entry &e = s_entries[nCode];
        if (!e.bindOpt) return -1;
        // explicitly cleared: the input is disabled regardless of the default
        // JOY mapping (return 0 = "not pressed", overriding the default path)
        if (e.bindOpt->getInteger() == KEYBIND_BIND_CLEARED) {
            return 0;
        }
        // effective binding: the custom binding, or the default JOY binding
        // for this logical key (so turbo works on default-bound keys too)
        int key = e.bindOpt->getInteger();
        bool custom = key >= 0;
        if (!custom) {
            key = e.defaultKey;
        }
        // pass default-bound rows through to the default mapping path unless
        // their turbo is enabled: directions rely on the analog-stick
        // emulation in CinpState, which this raw-key check would bypass
        if (!custom && !(e.turboOpt && e.turboOpt->getArrayIndex())) {
            return -1;
        }
        if (key < 0) return -1;   // no binding at all: use the default mapping

        if (!c2d_renderer) return 0;
        auto *input = c2d_renderer->getInput();
        auto *player = input ? input->getPlayer(e.port) : nullptr;
        if (!player || !player->enabled || !player->data) return 0;

        if (!input->getButtonState(*player, key)) return 0;

        if (e.turboOpt && e.turboOpt->getArrayIndex()) {
            int period = s_ratePeriod[s_rateIndex];
            int on = s_rateOnFrames[s_rateIndex];
            return ((int) (e.counter % (unsigned int) period) < on) ? 1 : 0;
        }
        return 1;
    }

    // ---------------------------------------------------------------------------
    // dialog accessors
    // ---------------------------------------------------------------------------
    int getRowCount() {
        return (int) s_rows.size();
    }

    // the physical button the JOY mapping assigns to a row's logical key (its
    // default binding; -1 when none / macro rows). resolved while the input
    // mapping is alive (build time) because the probe frees GameInp/sKeyBinds.
    int getDefaultKey(int row) {
        if (row < 0 || row >= (int) s_rows.size()) return -1;
        return s_rows[row].defaultKey;
    }

    const char *getRowName(int row) {
        if (row < 0 || row >= (int) s_rows.size()) return "";
        return s_rows[row].name.c_str();
    }

    c2d::config::Option *getBindOption(int row) {
        if (row < 0 || row >= (int) s_bindOpts.size()) return nullptr;
        return s_bindOpts[row];
    }

    c2d::config::Option *getTurboOption(int row) {
        if (row < 0 || row >= (int) s_turboOpts.size()) return nullptr;
        return s_turboOpts[row];
    }

    bool isTurboAllowed(int row) {
        if (row < 0 || row >= (int) s_rows.size()) return false;
        return s_rows[row].turboAllowed;
    }

    c2d::config::Option *getRateOption() {
        return s_rateOptCached;
    }

    int getEffectiveKey(int row) {
        if (row < 0 || row >= (int) s_rows.size()) return -1;
        if (row >= (int) s_bindOpts.size()) return -1;
        c2d::config::Option *bindOpt = s_bindOpts[row];
        if (!bindOpt) return -1;
        int key = bindOpt->getInteger();
        if (key == KEYBIND_BIND_CLEARED) return -1;   // explicitly cleared
        if (key >= 0) return key;                       // custom binding
        return s_rows[row].defaultKey;                  // default JOY mapping
    }

} // namespace pfbneo_keybind