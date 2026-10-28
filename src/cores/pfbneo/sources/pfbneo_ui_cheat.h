//
// pfbneo cheat dialog (金手指弹出对话框).
//
// Single-level list styled like the rom menu (UiMenu):
//   - one row per cheat: cheat name on the left, current option value on the
//     right (same layout as UiMenu menu lines);
//   - LEFT/RIGHT change the option of the highlighted cheat immediately
//     (applied to the running emulation, runtime-only);
//   - UP/DOWN move, list scrolls when there are more cheats than rows;
//   - B closes the dialog and resumes the game (the rom menu was already
//     hidden when the dialog was opened).
//   - name and value scroll independently (marquee) when their own text is
//     wider than the available space, same effect as the rom list.
//
// Runtime-only: selections are applied immediately and are never persisted.
//

#ifndef PFBN_UI_CHEAT_H
#define PFBN_UI_CHEAT_H

#include "skeleton/pemu.h"

class PfbneCheatDialog : public SkinnedRectangle {

public:
    explicit PfbneCheatDialog(pemu::UiMain *ui);

    void open();
    void close();

    bool onInput(c2d::Input::Player *players) override;

    void onUpdate() override;

    void onDraw(c2d::Transform &transform, bool draw) override;

private:
    struct Row {
        c2d::Text *name = nullptr;
        c2d::Text *value = nullptr;
        c2d::Color defaultColor{255, 255, 255, 255};  // skin color of the value text
        float defaultOutline = 0;                     // skin outline thickness of the name text
    };

    // independent marquee state for the name and the value column
    struct ScrollState {
        float offset = 0.f;
        int dir = 1;
        float holdUntil = 0.f;
    };

    void updateLines();
    void onKeyUp();
    void onKeyDown();
    bool isSelectable(int cheatIndex);

    // advance one marquee scroll state (bounce with pauses)
    static void scrollAdvance(ScrollState &st, float elapsed, float dt, float limit);

    pemu::UiMain *ui = nullptr;
    SkinnedText *title = nullptr;
    c2d::RectangleShape *highlight = nullptr;
    std::vector<Row> rows;
    std::vector<int> defaultOptions;  // snapshot of each cheat's default option (taken at open)
    int optionIndex = 0;
    int highlightIndex = 0;
    int maxLines = 0;
    float lineHeight = 0;
    int m_logCounter = 0;  // throttle render-trace logs

    // marquee scroll state (name and value scroll independently)
    c2d::C2DClock m_scrollClock;
    ScrollState m_scrollName;
    ScrollState m_scrollValue;
    float m_lastScrollTime = 0.f;
};

#endif // PFBN_UI_CHEAT_H
