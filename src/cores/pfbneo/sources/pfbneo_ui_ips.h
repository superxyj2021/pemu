//
// pfbneo IPS patch dialog (IPS补丁弹出对话框).
//
// Single-level list styled like the rom menu (UiMenu):
//   - one row per .dat patch file (title from the [zh_CN] info section),
//     preceded by non-selectable category header rows (the '/' category of
//     the PC tree-view becomes a plain row that cannot be selected);
//   - LEFT/RIGHT toggle the highlighted patch on/off immediately (green = on,
//     value column shows 启用/禁用);
//   - UP/DOWN move, skipping the category rows, list scrolls when needed;
//   - B saves the per-rom selections and returns to the ui menu with the
//     "IPS补丁" option highlighted (next game load applies the patches).
//

#ifndef PFBN_UI_IPS_H
#define PFBN_UI_IPS_H

#include "skeleton/pemu.h"

class PfbneIpsDialog : public SkinnedRectangle {

public:
    explicit PfbneIpsDialog(pemu::UiMain *ui);

    void open();
    void close();

    bool onInput(c2d::Input::Player *players) override;

    // toggle the rom list blur like the other dialogs
    void setVisibility(c2d::Visibility visibility, bool tweenPlay = false) override;

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
    bool isSelectable(int row);

    // nearest selectable row from "from" stepping by delta (+1 down, -1 up),
    // wrapping around the list ends; -1 when no row is selectable
    int findNextSelectable(int from, int delta);

    // position the scroll window so row "target" is visible (near the middle
    // of the window when possible, clamped to the list edges)
    void setWindowTo(int target);

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
    bool m_changed = false;   // a patch toggle changed in this dialog session

    // marquee scroll state (name and value scroll independently)
    c2d::C2DClock m_scrollClock;
    ScrollState m_scrollName;
    ScrollState m_scrollValue;
    float m_lastScrollTime = 0.f;
};

#endif // PFBN_UI_IPS_H