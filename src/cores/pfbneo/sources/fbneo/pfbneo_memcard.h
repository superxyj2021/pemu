//
// pfbneo memory card (记忆卡/插卡) support module.
//
// Port of the FBNeo Windows "memory card" feature for the two systems that
// expose an external (插拔) card interface through ACB_MEMCARD:
//   - NEOGEO (MVS/AES): all cartridge games, one external memory card (.fc);
//   - IGS PGM2: the SLE4442 IC card games (orleg2 / kov2nl / kov3 card
//     versions), up to 4 per-player slots (.bin, raw 264-byte card images).
//
// Card files live under <data path>/memcard/<romset name>/, PGM2 uses one
// sub-directory per player (P1..P4). Each slot stores the selected card file
// name in a per-rom "MEMCARD" config group ("" = empty slot).
//
// Insertion/ejection talks to the running core exactly like the Windows
// frontend: BurnAcb is set to a file callback and BurnAreaScan is called with
// (ACB_WRITE|ACB_MEMCARD|ACB_MEMCARD_ACTION) to insert and
// (ACB_READ|ACB_MEMCARD|ACB_MEMCARD_ACTION) to eject. When the game is not
// running the selection is only persisted; it is applied on game start.
//

#ifndef PFBNEO_MEMCARD_H
#define PFBNEO_MEMCARD_H

#include <string>

namespace ss_api {
    struct Game;
}

namespace c2d {
    namespace config {
        class Config;
        class Option;
    }
}

namespace pfbneo_memcard {

// id of the "MEMCARD" group inside the per-rom config. 0x5200+ does not
// collide with the shared ids nor with KEYBIND (0x5100) / DIP (0x5300).
#define MEMCARD_GROUP_ID   0x5200
#define MEMCARD_OPT_SLOT0  0x5201   // +slot: selected card file name ("" = empty)

    // --- lifecycle (called from the pfbneo core/config layers) -------------
    void init();
    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig);
    void onGameStart();                            // apply saved selections
    void onGameStop();

    // --- feature detection --------------------------------------------------
    bool hasMemcard();          // current game supports the external card?
    bool isPGM2();              // PGM2 IC card (vs. NEOGEO memory card)
    int  getSlotCount();        // 4 (PGM2) / 1 (NEOGEO)
    const char *getRomName();   // romset name (card directory name)

    // --- slot page (PGM2 4-player view) ------------------------------------
    int  countCards(int slot);                     // number of card files
    std::string getSlotName(int slot);             // "PGM2 卡槽 玩家 N：..."
    std::string getSlotValue(int slot);            // selected card or "空插槽"
    bool hasSelectedCard(int slot);

    // --- card selection list (0=空插槽 1=默认卡 2=创建新卡 3+=existing) -----
    int  getListCount(int slot);
    std::string getListName(int slot, int idx);
    bool isListCurrent(int slot, int idx);         // green marker
    int  getCurrentListIndex(int slot);            // where to place the cursor
    void applySelection(int slot, int idx);        // act + persist + insert/eject

    // --- insert/eject the selected card into the running core --------------
    void insertSlot(int slot);
    void ejectSlot(int slot);
}

#endif // PFBNEO_MEMCARD_H