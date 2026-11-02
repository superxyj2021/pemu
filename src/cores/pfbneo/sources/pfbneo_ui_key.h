//
// pfbneo key settings dialog (按键设置弹出对话框).
//
// Single-level list styled like the rom menu (UiMenu) and the cheat dialog:
//   - row 0 is the shared turbo rate (LEFT/RIGHT cycles 4Hz..30Hz);
//   - row 1 is the joystick deadzone (LEFT/RIGHT cycles 2000..30000);
//   - one row per logical key (player driver inputs in driver order + the
//     combo macros ported from the Windows build): Chinese name on the left,
//     the bound physical button icon in the middle, and a per-key turbo
//     on/off on the right;
//   - A on a key row captures a new physical button (same message box flow
//     as the old main-menu key mapping);
//   - LEFT/RIGHT toggle the turbo of the highlighted key (only when it has a
//     physical binding; directions and coin/select/start never get a turbo);
//   - UP/DOWN move, B closes and saves the per-rom config (the rom menu was
//     already hidden when the dialog was opened; closing resumes the game, or
//     re-opens the rom menu when no game is running).
//
// Bindings and turbo settings are per-rom config options ("KEYBIND" group),
// shared by all connected controllers (never per-controller), and are applied
// live by pfbneo_keybind::query()/tick().
//

#ifndef PFBN_UI_KEY_H
#define PFBN_UI_KEY_H

#include "skeleton/pemu.h"

class PfbneKeyDialog : public SkinnedRectangle {

public:
    explicit PfbneKeyDialog(pemu::UiMain *ui);

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
        c2d::Sprite *bind = nullptr;      // binding icon (physical button)
        c2d::Text *bindText = nullptr;    // fallback when no icon
        c2d::Text *turbo = nullptr;
        c2d::Color defaultColor{255, 255, 255, 255};
        float defaultOutline = 0;
    };

    // independent marquee scroll state for the name and the turbo column
    struct ScrollState {
        float offset = 0.f;
        int dir = 1;
        float holdUntil = 0.f;
    };

    void updateLines();
    void onKeyUp();
    void onKeyDown();
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

    // marquee scroll state (name and turbo scroll independently)
    c2d::C2DClock m_scrollClock;
    ScrollState m_scrollName;
    ScrollState m_scrollTurbo;
    float m_lastScrollTime = 0.f;
};

#endif // PFBN_UI_KEY_H