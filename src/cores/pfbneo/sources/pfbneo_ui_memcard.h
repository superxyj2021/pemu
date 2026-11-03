//
// pfbneo memory card dialog (记忆卡弹出对话框).
//
// Two pages styled like the cheat dialog:
//   - PGM2 games: a 4-row page (one per player slot) showing
//     "PGM2 卡槽 玩家 N：包含xx个角色卡文件" on the left and the currently
//     selected card name (or "空插槽") on the right. A on a row opens the
//     card selection list for that player.
//   - NEOGEO games: opens directly on the card selection list.
//   - Card selection list: 空插槽 / 默认卡 / 创建新卡, then every existing
//     card file in the slot directory. The row matching the currently
//     selected card is drawn in green and is where the cursor starts.
//   - A on a list row applies the selection (creating cards as needed),
//     returns to the slot page (PGM2) or closes the dialog (NEOGEO).
//   - B closes the dialog and saves the per-rom config.
//

#ifndef PFBN_UI_MEMCARD_H
#define PFBN_UI_MEMCARD_H

#include "skeleton/pemu.h"

class PfbneMemcardDialog : public SkinnedRectangle {

public:
    explicit PfbneMemcardDialog(pemu::UiMain *ui);

    void open();
    void close();

    bool onInput(c2d::Input::Player *players) override;

    // toggle the rom list blur like the other popup dialogs
    void setVisibility(c2d::Visibility visibility, bool tweenPlay = false) override;

    void onUpdate() override;

    void onDraw(c2d::Transform &transform, bool draw) override;

private:
    struct Row {
        c2d::Text *name = nullptr;
        c2d::Text *value = nullptr;
        c2d::Color defaultColor{255, 255, 255, 255};
        float defaultOutline = 0;
    };

    // independent marquee scroll state for the name and the value column
    struct ScrollState {
        float offset = 0.f;
        int dir = 1;
        float holdUntil = 0.f;
    };

    void updateLines();
    void onKeyUp();
    void onKeyDown();
    void setWindowTo(int target);

    // switch to the card selection list of "slot" (auto-places the cursor on
    // the currently selected card)
    void openSlotList(int slot);

    // advance one marquee scroll state (bounce with pauses)
    static void scrollAdvance(ScrollState &st, float elapsed, float dt, float limit);

    pemu::UiMain *ui = nullptr;
    SkinnedText *title = nullptr;
    c2d::RectangleShape *highlight = nullptr;
    std::vector<Row> rows;
    int optionIndex = 0;
    int highlightIndex = 0;
    int maxLines = 0;
    float lineHeight = 0;

    // page state: true = PGM2 slot page, false = card selection list
    bool m_slotPage = true;
    int m_activeSlot = 0;
    bool m_changed = false;   // a card selection changed in this dialog session

    // marquee scroll state (name and value scroll independently)
    c2d::C2DClock m_scrollClock;
    ScrollState m_scrollName;
    ScrollState m_scrollValue;
    float m_lastScrollTime = 0.f;
};

#endif // PFBN_UI_MEMCARD_H