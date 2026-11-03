//
// pfbneo move list (出招表) dialog - single-page accordion.
//
// One list showing every move group (from command.dat) as a row; pressing A
// on a move expands its command lines inline below it, pressing A again
// collapses them. Expanded state is session-local (reset on open).
//
//   - move rows: green + bold, value column shows 展开/收起;
//   - command rows: indented, skin color, full width - they ARE highlightable
//     so long inputs can be read with the marquee scroll;
//   - UP/DOWN move through every row (no skipping);
//   - B saves nothing and returns to the ui menu with "出招表" highlighted.
//

#ifndef PFBN_UI_MOVELIST_H
#define PFBN_UI_MOVELIST_H

#include "skeleton/pemu.h"

class PfbneMovelistDialog : public SkinnedRectangle {

public:
    explicit PfbneMovelistDialog(pemu::UiMain *ui);

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

    // one visible row: a move name (cmdIdx = -1) or one of its command lines
    struct RowRef {
        int moveIdx = 0;
        int cmdIdx = -1;
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

    // rebuild the visible row list from the expanded state (keeps the
    // previously highlighted move-name row at the same row index, since all
    // rows before it are unchanged)
    void rebuildRows();

    // position the scroll window so row "target" is visible (near the middle
    // of the window when possible, clamped to the list edges)
    void setWindowTo(int target);

    // advance one marquee scroll state (bounce with pauses)
    static void scrollAdvance(ScrollState &st, float elapsed, float dt, float limit);

    pemu::UiMain *ui = nullptr;
    SkinnedText *title = nullptr;
    c2d::RectangleShape *highlight = nullptr;
    std::vector<Row> rows;
    std::vector<RowRef> m_rows;       // dynamic visible row list
    std::vector<bool> m_expanded;     // per move group expand state
    int optionIndex = 0;
    int highlightIndex = 0;
    int maxLines = 0;
    float lineHeight = 0;

    // marquee scroll state (name and value scroll independently)
    c2d::C2DClock m_scrollClock;
    ScrollState m_scrollName;
    ScrollState m_scrollValue;
    float m_lastScrollTime = 0.f;
};

#endif // PFBN_UI_MOVELIST_H