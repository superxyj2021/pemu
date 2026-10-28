//
// Created by cpasjuste on 30/01/18.
//

#ifndef C2DUI_UI_MENU_NEW_H
#define C2DUI_UI_MENU_NEW_H

#include <memory>

class MenuLine;

namespace pemu {

    class UiMenu : public SkinnedRectangle {

    public:
        struct MenuOption {
            std::string name;
            c2d::config::Option *option = nullptr;
        };

        explicit UiMenu(UiMain *uiMain);

        ~UiMenu() override;

        void load(bool isRomMenu = false);

        UiMain *getUi() { return ui; };

        bool isRom() const { return isRomMenu; };

        virtual bool isOptionHidden(c2d::config::Option *option) { return false; };

        void onKeyUp();

        void onKeyDown();

        bool onInput(c2d::Input::Player *players) override;

        void onUpdate() override;

        void setVisibility(c2d::Visibility visibility, bool tweenPlay = false) override;

        // attach the donate dialog to the TOP of the ui tree (screen-space
        // coordinates, same as the cheat dialog). must be called after
        // UiMain::init, not from an input callback.
        void attachDonateDialog();

        // true while the donate dialog is open (used by UIRomList to yield
        // input so A cannot launch a rom / B reaches the dialog)
        bool isDonateDialogOpen();

    private:
        void updateLines();

        // shrink the menu box (and re-center vertically) to fit the current
        // number of items, never exceeding the original skin height
        void fitMenuHeight();

        UiMain *ui = nullptr;
        SkinnedText *title = nullptr;
        c2d::RectangleShape *highlight = nullptr;
        std::vector<MenuLine *> lines;
        float alpha = 230;
        float m_origMenuHeight = 0.f;

        std::vector<MenuOption> menu_options;
        c2d::TweenPosition *tweenPosition;
        Skin::TextGroup textGroup;

        float lineHeight = 0;
        int maxLines = 0;
        int optionIndex = 0;
        int highlightIndex = 0;

        bool isRomMenu = false;
        bool isEmuRunning = false;
        bool needSave = false;

        // donate dialog ("打赏作者" item, A opens it, B closes it). owned by
        // the menu but ATTACHED to the top of the ui tree (screen coords).
        class DonateDialog *donateDialog = nullptr;
        bool m_donateDialogAttached = false;

        // marquee scroll state for the highlighted line.
        // the line's name (left) and value (right) column scroll
        // INDEPENDENTLY: only the side that is too long moves.
        struct ScrollState {
            float offset = 0.f;
            int dir = 1;
            float holdUntil = 0.f;
        };
        c2d::C2DClock m_scrollClock;
        ScrollState m_scrollName;
        ScrollState m_scrollValue;
        float m_lastScrollTime = 0.f;
    };
}

#endif //C2DUI_UI_MENU_NEW_H
