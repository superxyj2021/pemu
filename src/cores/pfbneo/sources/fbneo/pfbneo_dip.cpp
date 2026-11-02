//
// pfbneo DIP switch (DIP 开关) support module implementation.
//
// See pfbneo_dip.h for the design notes.
//
// The switch/option model follows the FBNeo burner DIP list layout:
//   - 0xF0 entry  : nDIPOffset (index of the DIP block inside GameInp[]);
//   - 0xFE / 0xFD : group header, szText = switch name, nSetting = option count;
//   - 0xFF        : default override, nConst = (nConst & ~mask) | (setting & mask);
//   - other flags : an option of the current group (primary entry only; like
//     the SDL burner client, matching/apply use the primary mask/setting).
//

// NOTE on include order: "skeleton/pemu.h" MUST come first, same rule as
// pfbneo_keybind.cpp (it pulls in ss_api.h, cross2d/c2d.h, pemu_config.h,
// ui_text_def.h AND the "using namespace ss_api").

#include "skeleton/pemu.h"
#include "pfbneo_dip.h"
#include "pfbneo_utility.h"
#include "burner.h"
#include "retro_input_wrapper.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace c2d;

// globals exported from the core input layer (burner.h / retro_input*), must
// stay at global scope (NOT inside the namespace below)
extern struct GameInp *GameInp;

namespace pfbneo_dip {

    struct DipOption {
        std::string text;        // option text (original English)
        int input = 0;           // DIP block index (relative to nDIPOffset)
        int mask = 0;
        int setting = 0;
        unsigned char flags = 0;
    };

    struct DipSwitch {
        std::string name;        // group header text (original English)
        std::vector<DipOption> options;
        int defaultIndex = 0;    // factory default (from the 0xFF overrides)
    };

    static std::vector<DipSwitch> s_switches;
    static std::vector<c2d::config::Option *> s_valueOpts;  // per switch
    static c2d::config::Config *s_gameConfig = nullptr;
    static int s_nDIPOffset = 0;

    // ---------------------------------------------------------------------------
    // Chinese display-name translation table (Chinese (Simplified)_dips.glt)
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

        static const char *FILE_NAME = "localisation/Chinese (Simplified)_dips.glt";
        std::string path = io->getDataPath() + FILE_NAME;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            path = io->getRomFsPath() + FILE_NAME;
            file.open(path, std::ios::binary);
        }
        if (!file.is_open()) {
            printf("pfbneo_dip: no DIP translation table (%s), using English names\n", FILE_NAME);
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

        printf("pfbneo_dip: DIP translation table: %s, entries: %zu\n",
               path.c_str(), s_translations.size());
        return !s_translations.empty();
    }

    std::string getDisplayName(const std::string &text) {
        if (!loadTranslationTable()) {
            return text;
        }
        auto it = s_translations.find(text);
        if (it != s_translations.end()) {
            return it->second;
        }
        return text;   // not translated: keep the original English text
    }

    // ---------------------------------------------------------------------------
    // switch enumeration (driver DIP list)
    // ---------------------------------------------------------------------------
    // Returns the number of switches. The driver must already be active
    // (setDriverActive), the DIP list is static driver data.
    static void buildSwitches() {
        s_switches.clear();
        s_nDIPOffset = 0;

        BurnDIPInfo bdi;
        // first pass: the 0xFF "default override" entries -> the factory
        // default value of every DIP block byte (applied over an all-zero
        // block, exactly what InpDIPSWResetDIPs() produces when the driver
        // initialises its DIP memory to 0)
        std::unordered_map<int, int> defaults;
        for (int i = 0; BurnDrvGetDIPInfo(&bdi, i) == 0; i++) {
            if (bdi.nFlags == 0xF0) {
                s_nDIPOffset = bdi.nInput;
            } else if (bdi.nFlags == 0xFF) {
                int cur = defaults[bdi.nInput];
                defaults[bdi.nInput] = (cur & ~bdi.nMask) | (bdi.nSetting & bdi.nMask);
            }
        }

        // second pass: group headers + options
        int cur = -1;
        for (int i = 0; BurnDrvGetDIPInfo(&bdi, i) == 0; i++) {
            if (bdi.nFlags == 0xF0) {
                continue;
            }
            if (bdi.nFlags == 0xFF) {
                continue;   // already handled in the first pass
            }
            if (!bdi.szText) {
                continue;   // unnamed entries (compound span extras, offsets)
            }
            if (bdi.nFlags & 0x40) {
                // group header (0xFE / 0xFD)
                DipSwitch sw;
                sw.name = bdi.szText;
                sw.defaultIndex = 0;
                s_switches.push_back(sw);
                cur = (int) s_switches.size() - 1;
                continue;
            }
            if (cur >= 0) {
                DipOption opt;
                opt.text = bdi.szText;
                opt.input = bdi.nInput;
                opt.mask = bdi.nMask;
                opt.setting = bdi.nSetting;
                opt.flags = (unsigned char) bdi.nFlags;
                s_switches[cur].options.push_back(opt);
            }
        }

        // factory default index of every switch: the first option whose
        // (mask, setting) matches the default byte value
        for (size_t s = 0; s < s_switches.size(); s++) {
            DipSwitch &sw = s_switches[s];
            int defIdx = 0;
            for (size_t o = 0; o < sw.options.size(); o++) {
                auto it = defaults.find(sw.options[o].input);
                int v = it != defaults.end() ? it->second : 0;
                if ((v & sw.options[o].mask) == sw.options[o].setting) {
                    defIdx = (int) o;
                    break;
                }
            }
            sw.defaultIndex = defIdx;
        }
    }

    // ---------------------------------------------------------------------------
    // value apply (write the selected option bits into the running GameInp[])
    // ---------------------------------------------------------------------------
    static void applyOption(int switchIndex, int optionIndex) {
        if (switchIndex < 0 || switchIndex >= (int) s_switches.size()) return;
        DipSwitch &sw = s_switches[switchIndex];
        if (optionIndex < 0 || optionIndex >= (int) sw.options.size()) return;
        if (!GameInp) return;
        if (s_nDIPOffset < 0) return;

        DipOption &opt = sw.options[optionIndex];
        struct GameInp *pgi = GameInp + opt.input + s_nDIPOffset;
        pgi->Input.Constant.nConst = (pgi->Input.Constant.nConst & ~opt.mask) | (opt.setting & opt.mask);
    }

    // ---------------------------------------------------------------------------
    // lifecycle
    // ---------------------------------------------------------------------------
    void init() {
        s_switches.clear();
        s_valueOpts.clear();
        s_nDIPOffset = 0;
    }

    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig) {
        s_gameConfig = gameConfig;
        s_switches.clear();
        s_valueOpts.clear();
        s_nDIPOffset = 0;

        // make sure the requested driver is active (the DIP list is static
        // driver data; no probe / GameInp needed to enumerate it)
        PFBNEOUtility::setDriverActive(game);
        buildSwitches();

        // create the per-rom "DIP" config group (one array option per switch)
        if (!s_switches.empty()) {
            c2d::config::Group dipGroup("DIP", DIP_GROUP_ID, TEXT_MENU_DIP);
            for (size_t s = 0; s < s_switches.size(); s++) {
                std::vector<std::string> values;
                for (size_t o = 0; o < s_switches[s].options.size(); o++) {
                    values.emplace_back(getDisplayName(s_switches[s].options[o].text));
                }
                std::string key = std::to_string((long long) s);
                dipGroup.addOption({"DIP_OPT_" + key, values, s_switches[s].defaultIndex,
                                    DIP_OPT_BASE + (int) s, "", getDisplayName(s_switches[s].name)});
            }
            gameConfig->addGroup(dipGroup);
        }

        // cache the option pointers (the config object is recreated per game /
        // per menu open, so re-cache every load)
        for (size_t s = 0; s < s_switches.size(); s++) {
            s_valueOpts.push_back(gameConfig->getOption(DIP_OPT_BASE + (int) s));
        }
    }

    void onGameStart() {
        // apply every switch's saved selection to the running GameInp[] DIP
        // constants (called right after InputInit; untouched switches are at
        // their factory default so this is a no-op for them)
        if (!GameInp) return;
        for (size_t s = 0; s < s_switches.size() && s < s_valueOpts.size(); s++) {
            c2d::config::Option *opt = s_valueOpts[s];
            if (!opt) continue;
            applyOption((int) s, opt->getArrayIndex());
        }
    }

    void onGameStop() {
        s_gameConfig = nullptr;
        s_switches.clear();
        s_valueOpts.clear();
        s_nDIPOffset = 0;
    }

    // ---------------------------------------------------------------------------
    // dialog accessors
    // ---------------------------------------------------------------------------
    int getRowCount() {
        return (int) s_switches.size();
    }

    const char *getRowName(int row) {
        if (row < 0 || row >= (int) s_switches.size()) return "";
        return s_switches[row].name.c_str();
    }

    c2d::config::Option *getValueOption(int row) {
        if (row < 0 || row >= (int) s_valueOpts.size()) return nullptr;
        return s_valueOpts[row];
    }

    bool isChanged(int row) {
        if (row < 0 || row >= (int) s_switches.size()) return false;
        c2d::config::Option *opt = getValueOption(row);
        if (!opt) return false;
        return !opt->isDefault();
    }

    int getDefaultIndex(int row) {
        if (row < 0 || row >= (int) s_switches.size()) return 0;
        return s_switches[row].defaultIndex;
    }

    void applyRow(int row) {
        c2d::config::Option *opt = getValueOption(row);
        if (!opt) return;
        applyOption(row, opt->getArrayIndex());
    }

} // namespace pfbneo_dip