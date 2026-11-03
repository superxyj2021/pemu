//
// pfbneo DIP switch dialog (DIP 开关弹出对话框).
//
// Single-level list styled like the key settings dialog / cheat dialog:
//   - one row per DIP switch of the running (or selected) game: Chinese
//     switch name on the left, the currently selected option on the right;
//   - LEFT/RIGHT cycle the switch's options (live-applied to the running
//     game when one is running);
//   - a row whose value differs from the factory default is drawn green;
//   - UP/DOWN move, B closes and saves the per-rom config.
//
// When the game has no DIP switches an empty message is shown. The dialog
// works from the rom menu (game not started: settings are persisted and
// applied at game start) and from the in-game menu (applied live).
//

#ifndef PFBN_UI_DIP_H
#define PFBN_UI_DIP_H

#include "skeleton/pemu.h"

class PfbneDipDialog : public SkinnedRectangle {

public:
    explicit PfbneDipDialog(pemu::UiMain *ui);

    void open();
    void close();

    bool onInput(c2d::Input::Player *players) override;

    // toggle the rom list blur like the save-states dialog (keeps the main
    // ui blurred behind the dialog when opened from the rom screen)
    void setVisibility(c2d::Visibility visibility, bool tweenPlay = false) override;

    void onUpdate() override;

    void onDraw(c2d::Transform &transform, bool draw) override;

private:
    struct Row {
        c2d::Text *name = nullptr;
        c2d::Text *value = nullptr;
        c2d::Color defaultColor{255, 255, 255, 255};
    };

    struct ScrollState {
        float offset = 0.f;
        int dir = 1;
        float holdUntil = 0.f;
    };

    void updateLines();
    void onKeyUp();
    void onKeyDown();
    void setWindowTo(int target);

    static void scrollAdvance(ScrollState &st, float elapsed, float dt, float limit);

    pemu::UiMain *ui = nullptr;
    SkinnedText *title = nullptr;
    c2d::Text *emptyText = nullptr;
    c2d::RectangleShape *highlight = nullptr;
    std::vector<Row> rows;
    int optionIndex = 0;
    int highlightIndex = 0;
    int maxLines = 0;
    bool m_changed = false;   // a switch was modified in this dialog session
    float lineHeight = 0;

    c2d::C2DClock m_scrollClock;
    ScrollState m_scrollName;
    ScrollState m_scrollValue;
    float m_lastScrollTime = 0.f;
};

#endif // PFBN_UI_DIP_H