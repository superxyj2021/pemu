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
		
		void showDonateImage();
		void hideDonateImage();

    private:
        void updateLines();

        UiMain *ui = nullptr;
        SkinnedText *title = nullptr;
        c2d::RectangleShape *highlight = nullptr;
        std::vector<MenuLine *> lines;
        float alpha = 230;

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
		

        c2d::Texture *pDonateImage = nullptr;
		bool m_showingDonate = false;
		c2d::RectangleShape *pBlur = nullptr;
    };
}

#endif //C2DUI_UI_MENU_NEW_H
