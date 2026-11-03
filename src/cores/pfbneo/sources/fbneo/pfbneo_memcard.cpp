//
// pfbneo memory card (记忆卡/插卡) support module implementation.
//
// See pfbneo_memcard.h for the design notes.
//

// NOTE on include order: "skeleton/pemu.h" MUST come first, same rule as
// pfbneo_keybind.cpp / pfbneo_dip.cpp (it pulls in ss_api.h, cross2d/c2d.h,
// pemu_config.h, ui_text_def.h AND the "using namespace ss_api").

#include "skeleton/pemu.h"
#include "pfbneo_memcard.h"
#include "pfbneo_utility.h"
#include "burner.h"
#include "burnint.h"
#include "state.h"

#include <cstring>
#include <cstdio>
#include <cctype>
#include <ctime>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

using namespace c2d;

// PGM2 card globals (defined in the external pgm2 driver; forward-declared
// here instead of including the whole pgm2.h driver header into this module).
extern INT32 Pgm2ActiveCardSlot;
extern INT32 pgm2GetCardRomTemplate(UINT8* buffer, INT32 maxSize);

namespace pfbneo_memcard {

    static const int MAX_SLOTS = 4;
    static const int PGM2_CARD_SIZE = 0x108;      // SLE4442: 256 data + 4 prot + 4 sec
    static const int NEO_CARD_REGION = 0x20000;   // NEOGEO memory card window
    static const int NEO_MEMCARD_MIN_VERSION = 0x029713;

    static c2d::config::Config *s_gameConfig = nullptr;
    static c2d::config::Option *s_slotOpts[MAX_SLOTS] = {};
    static bool s_hasMemcard = false;
    static bool s_isPGM2 = false;
    static int  s_slotCount = 0;
    static int  s_cardSize = 0;
    static std::string s_romName;
    static bool s_gameRunning = false;

    // per-slot .fc container format (FB Alpha FC1 vs. MAME interleaved),
    // detected when a card file is read / created
    static bool s_fc1Format[MAX_SLOTS] = {};

    // context for the global BurnAcb callback
    static int  s_ioSlot = 0;
    static bool s_ioInsert = false;

    // ---------------------------------------------------------------------------
    // file helpers
    // ---------------------------------------------------------------------------
    static std::string dataPath() {
        if (!c2d_renderer || !c2d_renderer->getIo()) return "./";
        std::string p = c2d_renderer->getIo()->getDataPath();
        return p.empty() ? "./" : p;
    }

    static std::string slotDir(int slot) {
        std::string dir = dataPath() + "memcards/" + s_romName + "/";
        if (s_isPGM2) {
            dir += "P" + std::to_string((long long) slot + 1) + "/";
        }
        return dir;
    }

    static std::string defaultCardName() {
        return s_isPGM2 ? "memcard.bin" : "memcard.fc";
    }

    static bool fileExists(const std::string &path) {
        FILE *fp = fopen(path.c_str(), "rb");
        if (fp) {
            fclose(fp);
            return true;
        }
        return false;
    }

    static void ensureDir(const std::string &path) {
        std::string p = path;
        while (!p.empty() && p.back() == '/') {
            p.pop_back();
        }
        if (p.empty()) return;
        size_t pos = 0;
        while ((pos = p.find('/', pos + 1)) != std::string::npos) {
            mkdir(p.substr(0, pos).c_str(), 0777);
        }
        mkdir(p.c_str(), 0777);
    }

    static std::string newCardName() {
        char ts[32] = "";
        time_t t = time(nullptr);
        struct tm *tmv = localtime(&t);
        if (tmv) {
            strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tmv);
        } else {
            snprintf(ts, sizeof(ts), "%lld", (long long) t);
        }
        return s_romName + "_" + ts + (s_isPGM2 ? ".bin" : ".fc");
    }

    // list the card files (.bin / .fc) of a slot; the default card is shown
    // by its own "默认卡" row and is not listed again
    static std::vector<std::string> listCards(int slot) {
        std::vector<std::string> out;
        std::string dir = slotDir(slot);
        std::string ext = s_isPGM2 ? ".bin" : ".fc";
        DIR *d = opendir(dir.c_str());
        if (!d) return out;
        struct dirent *e;
        while ((e = readdir(d)) != nullptr) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            size_t dot = name.rfind('.');
            if (dot == std::string::npos) continue;
            std::string e2 = name.substr(dot);
            std::transform(e2.begin(), e2.end(), e2.begin(), ::tolower);
            if (e2 != ext) continue;
            if (name == defaultCardName()) continue;
            out.push_back(name);
        }
        closedir(d);
        std::sort(out.begin(), out.end());
        return out;
    }

    // ---------------------------------------------------------------------------
    // card file format handling
    // ---------------------------------------------------------------------------
    // NEOGEO .fc file, "FB Alpha memory card" (FC1) container:
    //   "FB1 " "FC1 " + chunk size (data+32) + version + min version + 3 zero + data
    static bool isFc1File(const std::string &path) {
        FILE *fp = fopen(path.c_str(), "rb");
        if (!fp) return false;
        char hdr[8];
        bool fc1 = (fread(hdr, 1, 8, fp) == 8 && memcmp(hdr, "FB1 FC1 ", 8) == 0);
        fclose(fp);
        return fc1;
    }

    static void createCardFile(int slot, const std::string &path) {
        if (s_isPGM2) {
            UINT8 card[PGM2_CARD_SIZE];
            int len = pgm2GetCardRomTemplate(card, (int) sizeof(card));
            if (len <= 0) {
                // blank SLE4442 card: all 0xFF, 3 auth attempts, default PSC
                memset(card, 0xFF, sizeof(card));
                card[0x104] = 0x07;
                len = PGM2_CARD_SIZE;
            }
            FILE *fp = fopen(path.c_str(), "wb");
            if (fp) {
                fwrite(card, 1, (size_t) len, fp);
                fclose(fp);
            }
        } else {
            // NEOGEO: FC1 container with a blank (zeroed) card area
            const int nSize = NEO_CARD_REGION;
            const int nChunkSize = nSize + 32;
            FILE *fp = fopen(path.c_str(), "wb");
            if (fp) {
                fwrite("FB1 ", 1, 4, fp);
                fwrite("FC1 ", 1, 4, fp);
                fwrite(&nChunkSize, 4, 1, fp);
                INT32 v = nBurnVer;
                fwrite(&v, 4, 1, fp);
                v = NEO_MEMCARD_MIN_VERSION;
                fwrite(&v, 4, 1, fp);
                INT32 z = 0;
                for (int i = 0; i < 3; i++) {
                    fwrite(&z, 4, 1, fp);
                }
                std::vector<UINT8> blank((size_t) nSize, 0);
                fwrite(blank.data(), 1, nSize, fp);
                fclose(fp);
            }
            s_fc1Format[slot] = true;
        }
    }

    // insert: file -> card buffer
    static void readCardFile(int slot, const std::string &path, UINT8 *data, int nSize) {
        FILE *fp = fopen(path.c_str(), "rb");
        if (!fp || nSize <= 0) {
            if (fp) fclose(fp);
            return;
        }
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (s_isPGM2) {
            // raw SLE4442 image (264 or 256 bytes)
            memset(data, 0xFF, (size_t) nSize);
            size_t n = (size_t) (len <= nSize ? len : nSize);
            fread(data, 1, n, fp);
            if (len == 0x100 && nSize >= PGM2_CARD_SIZE) {
                data[0x100] = 0xFF; data[0x101] = 0xFF;
                data[0x102] = 0xFF; data[0x103] = 0xFF;
                data[0x104] = 0x07;
                data[0x105] = 0xFF; data[0x106] = 0xFF; data[0x107] = 0xFF;
            }
        } else {
            char hdr[8];
            size_t got = fread(hdr, 1, 8, fp);
            if (got == 8 && memcmp(hdr, "FB1 FC1 ", 8) == 0) {
                // FB Alpha memory card file
                int nChunkSize = 0;
                fread(&nChunkSize, 1, 4, fp);
                fseek(fp, 0x0C, SEEK_CUR);   // skip version fields + reserved
                int dataSize = nChunkSize - 32;
                if (dataSize > nSize) dataSize = nSize;
                memset(data, 0, (size_t) nSize);
                if (dataSize > 0) fread(data, 1, (size_t) dataSize, fp);
                s_fc1Format[slot] = true;
            } else if (len == ((long) nSize >> 1)) {
                // MAME Neo Geo half-size interleaved file: odd bytes only
                memset(data, 0, (size_t) nSize);
                fseek(fp, 0, SEEK_SET);
                size_t max = (size_t) (nSize >> 1);
                size_t cap = (len < (long) max) ? (size_t) len : max;
                std::vector<UINT8> tmp(cap);
                size_t rd = fread(tmp.data(), 1, cap, fp);
                for (size_t i = 0; i < rd; i++) {
                    data[i * 2 + 1] = tmp[i];
                }
                s_fc1Format[slot] = false;
            } else {
                // raw card image of any other size (kept for robustness)
                memset(data, 0xFF, (size_t) nSize);
                fseek(fp, 0, SEEK_SET);
                size_t n = (size_t) (len <= nSize ? len : nSize);
                fread(data, 1, n, fp);
                s_fc1Format[slot] = false;
            }
        }
        fclose(fp);
    }

    // eject: card buffer -> file
    static void writeCardFile(int slot, const std::string &path, UINT8 *data, int nSize) {
        FILE *fp = fopen(path.c_str(), "wb");
        if (!fp || nSize <= 0) {
            if (fp) fclose(fp);
            return;
        }
        if (s_isPGM2) {
            fwrite(data, 1, (size_t) nSize, fp);
        } else if (s_fc1Format[slot]) {
            int nChunkSize = nSize + 32;
            fwrite("FB1 ", 1, 4, fp);
            fwrite("FC1 ", 1, 4, fp);
            fwrite(&nChunkSize, 4, 1, fp);
            INT32 v = nBurnVer;
            fwrite(&v, 4, 1, fp);
            v = NEO_MEMCARD_MIN_VERSION;
            fwrite(&v, 4, 1, fp);
            INT32 z = 0;
            for (int i = 0; i < 3; i++) {
                fwrite(&z, 4, 1, fp);
            }
            fwrite(data, 1, (size_t) nSize, fp);
        } else {
            // MAME Neo Geo half-size interleaved file: odd bytes only
            for (int i = 1; i < nSize; i += 2) {
                fwrite(data + i, 1, 1, fp);
            }
        }
        fclose(fp);
    }

    // ---------------------------------------------------------------------------
    // BurnAreaScan bridge (same mechanism as the Windows frontend memcard.cpp)
    // ---------------------------------------------------------------------------
    static INT32 __cdecl memcardIoCb(struct BurnArea *pba) {
        if (!pba || !pba->Data || pba->nLen <= 0) return 0;
        int slot = s_ioSlot;
        if (slot < 0 || slot >= MAX_SLOTS) return 0;
        c2d::config::Option *opt = s_slotOpts[slot];
        std::string file = opt ? opt->getString() : "";
        if (file.empty()) return 0;
        std::string path = slotDir(slot) + file;
        if (!fileExists(path)) return 0;
        if (s_ioInsert) {
            readCardFile(slot, path, (UINT8 *) pba->Data, (int) pba->nLen);
        } else {
            writeCardFile(slot, path, (UINT8 *) pba->Data, (int) pba->nLen);
        }
        return 0;
    }

    void insertSlot(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return;
        if (!s_gameRunning || !s_hasMemcard) return;
        c2d::config::Option *opt = s_slotOpts[slot];
        if (!opt || opt->getString().empty()) return;
        if (s_isPGM2) Pgm2ActiveCardSlot = slot;
        s_ioSlot = slot;
        s_ioInsert = true;
        INT32 nMinVersion = 0;
        BurnAcb = memcardIoCb;
        BurnAreaScan(ACB_WRITE | ACB_MEMCARD | ACB_MEMCARD_ACTION, &nMinVersion);
        BurnAcb = nullptr;
    }

    void ejectSlot(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return;
        if (!s_gameRunning || !s_hasMemcard) return;
        c2d::config::Option *opt = s_slotOpts[slot];
        if (!opt || opt->getString().empty()) return;
        if (s_isPGM2) Pgm2ActiveCardSlot = slot;
        s_ioSlot = slot;
        s_ioInsert = false;
        INT32 nMinVersion = 0;
        BurnAcb = memcardIoCb;
        BurnAreaScan(ACB_READ | ACB_MEMCARD | ACB_MEMCARD_ACTION, &nMinVersion);
        BurnAcb = nullptr;
    }

    // ---------------------------------------------------------------------------
    // lifecycle
    // ---------------------------------------------------------------------------
    void init() {
        for (int i = 0; i < MAX_SLOTS; i++) {
            s_slotOpts[i] = nullptr;
            s_fc1Format[i] = false;
        }
        s_gameConfig = nullptr;
        s_hasMemcard = false;
        s_isPGM2 = false;
        s_slotCount = 0;
        s_cardSize = 0;
        s_romName.clear();
        s_gameRunning = false;
    }

    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig) {
        s_gameConfig = gameConfig;
        for (int i = 0; i < MAX_SLOTS; i++) {
            s_slotOpts[i] = nullptr;
            s_fc1Format[i] = false;
        }
        s_hasMemcard = false;
        s_isPGM2 = false;
        s_slotCount = 0;
        s_cardSize = 0;
        s_romName.clear();

        // detect whether the game uses an external memory card: the card
        // interface is a hardware feature of the NEOGEO (MVS/AES) cartridge
        // system, while PGM2 only has it when the rom set carries a 0x108
        // byte "blank card" template (orleg2/kov2nl/kov3 card versions).
        PFBNEOUtility::setDriverActive(game);
        UINT32 hw = BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK;
        if (hw == HARDWARE_SNK_NEOGEO || hw == HARDWARE_SNK_MVS) {
            s_hasMemcard = true;
            s_isPGM2 = false;
            s_slotCount = 1;
            s_cardSize = NEO_CARD_REGION;
        } else if (hw == HARDWARE_IGS_PGM2) {
            struct BurnRomInfo ri;
            for (UINT32 i = 0; BurnDrvGetRomInfo(&ri, i) == 0; i++) {
                if (ri.nLen == 0x108 && (ri.nType & BRF_OPT)) {
                    s_hasMemcard = true;
                    break;
                }
            }
            if (s_hasMemcard) {
                s_isPGM2 = true;
                s_slotCount = 4;
                s_cardSize = PGM2_CARD_SIZE;
            }
        }
        if (!s_hasMemcard) return;

        const char *name = BurnDrvGetTextA(DRV_NAME);
        s_romName = name ? name : "";

        // per-rom "MEMCARD" config group: one string option per slot holding
        // the selected card file name ("" = empty slot)
        c2d::config::Group group("MEMCARD", MEMCARD_GROUP_ID, TEXT_MENU_MEMCARD);
        for (int s = 0; s < MAX_SLOTS; s++) {
            std::string key = "MEMCARD_SLOT" + std::to_string((long long) s);
            group.addOption({key, "", MEMCARD_OPT_SLOT0 + s, "", TEXT_MENU_MEMCARD});
        }
        gameConfig->addGroup(group);

        for (int s = 0; s < MAX_SLOTS; s++) {
            s_slotOpts[s] = gameConfig->getOption(MEMCARD_OPT_SLOT0 + s);
        }
    }

    void onGameStart() {
        s_gameRunning = true;
        if (!s_hasMemcard) return;
        // apply the saved selections: insert every slot that has a card
        for (int s = 0; s < s_slotCount; s++) {
            c2d::config::Option *opt = s_slotOpts[s];
            if (opt && !opt->getString().empty()) {
                insertSlot(s);
            }
        }
    }

    void onGameStop() {
        s_gameRunning = false;
        s_gameConfig = nullptr;
        for (int i = 0; i < MAX_SLOTS; i++) {
            s_slotOpts[i] = nullptr;
            s_fc1Format[i] = false;
        }
        s_hasMemcard = false;
        s_isPGM2 = false;
        s_slotCount = 0;
        s_cardSize = 0;
        s_romName.clear();
    }

    // ---------------------------------------------------------------------------
    // dialog accessors
    // ---------------------------------------------------------------------------
    bool hasMemcard() {
        return s_hasMemcard;
    }

    bool isPGM2() {
        return s_isPGM2;
    }

    int getSlotCount() {
        return s_slotCount;
    }

    const char *getRomName() {
        return s_romName.c_str();
    }

int countCards(int slot) {
    if (slot < 0 || slot >= MAX_SLOTS) return 0;
    // count every card file in the slot directory, INCLUDING the default card
    // (memcard.bin / memcard.fc) which listCards() filters out
    int n = (int) listCards(slot).size();
    if (fileExists(slotDir(slot) + defaultCardName())) {
        n++;
    }
    return n;
}

    std::string getSlotName(int slot) {
        char buf[160] = "";
        snprintf(buf, sizeof(buf), TEXT_MEMCARD_SLOT_FMT, slot + 1, countCards(slot));
        return buf;
    }

    std::string getSlotValue(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return "";
        c2d::config::Option *opt = s_slotOpts[slot];
        std::string sel = opt ? opt->getString() : "";
        if (sel.empty()) return TEXT_MEMCARD_EMPTY;
        return sel;
    }

    bool hasSelectedCard(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return false;
        c2d::config::Option *opt = s_slotOpts[slot];
        return opt && !opt->getString().empty();
    }

    int getListCount(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return 0;
        return 3 + (int) listCards(slot).size();
    }

    std::string getListName(int slot, int idx) {
        if (slot < 0 || slot >= MAX_SLOTS || idx < 0) return "";
        if (idx == 0) return TEXT_MEMCARD_EMPTY;
        if (idx == 1) return TEXT_MEMCARD_DEFAULT;
        if (idx == 2) return TEXT_MEMCARD_CREATE;
        auto files = listCards(slot);
        size_t i = (size_t) (idx - 3);
        if (i < files.size()) return files[i];
        return "";
    }

    bool isListCurrent(int slot, int idx) {
        if (slot < 0 || slot >= MAX_SLOTS) return false;
        c2d::config::Option *opt = s_slotOpts[slot];
        std::string sel = opt ? opt->getString() : "";
        if (idx == 0) return sel.empty();
        if (idx == 1) return sel == defaultCardName();
        if (idx == 2) return false;   // "创建新卡" is never the current card
        auto files = listCards(slot);
        size_t i = (size_t) (idx - 3);
        return i < files.size() && files[i] == sel;
    }

    int getCurrentListIndex(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) return 0;
        c2d::config::Option *opt = s_slotOpts[slot];
        std::string sel = opt ? opt->getString() : "";
        if (sel.empty()) return 0;
        if (sel == defaultCardName()) return 1;
        auto files = listCards(slot);
        for (size_t i = 0; i < files.size(); i++) {
            if (files[i] == sel) return (int) i + 3;
        }
        return 0;   // selected file is gone: fall back to 空插槽
    }

    void applySelection(int slot, int idx) {
        if (slot < 0 || slot >= MAX_SLOTS) return;
        c2d::config::Option *opt = s_slotOpts[slot];
        if (!opt) return;
        int total = getListCount(slot);
        if (idx < 0 || idx >= total) return;

        // eject the current card first (writes its data back to its file)
        ejectSlot(slot);

        std::string newFile;
        if (idx == 0) {
            // 空插槽: nothing selected
        } else if (idx == 1) {
            // 默认卡: memcard.bin/.fc, created when missing
            newFile = defaultCardName();
            std::string path = slotDir(slot) + newFile;
            if (!fileExists(path)) {
                ensureDir(slotDir(slot));
                createCardFile(slot, path);
            }
        } else if (idx == 2) {
            // 创建新卡: rom name + timestamp
            newFile = newCardName();
            ensureDir(slotDir(slot));
            createCardFile(slot, slotDir(slot) + newFile);
        } else {
            auto files = listCards(slot);
            size_t i = (size_t) (idx - 3);
            if (i >= files.size()) return;
            newFile = files[i];
        }

        opt->setString(newFile);
        if (!newFile.empty()) {
            insertSlot(slot);
        }
    }

} // namespace pfbneo_memcard