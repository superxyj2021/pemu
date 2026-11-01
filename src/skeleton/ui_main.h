//
// Created by cpasjuste on 14/01/18.
//

#ifndef GUI_H
#define GUI_H

#define INPUT_DELAY 200

// startup splash: the skin title image is shown for at least this many
// seconds on every launch (recommended value, adjust freely)
#define SPLASH_DURATION 2.0f
// splash image size relative to the screen (1.0 = full screen, 0.8 = 80%)
#define SPLASH_SCALE 0.8f

namespace pemu {

    class UiMain : public c2d::C2DRenderer {

    public:
        explicit UiMain(const c2d::Vector2f &size = c2d::Vector2f(0, 0));

        void init(UIRomList *romList, UiMenu *menu, UiEmu *emu, UiMenuState *state);

        // creates the load-game progress dialog. normally done inside init(),
        // but the first rom scan may run before init(): the startup flow calls
        // this right after the skin is loaded. idempotent.
        void createProgressBox();

        // shows the startup splash (full-screen skin title image). called as
        // early as possible (right after the skin is loaded); it stays on
        // screen for at least SPLASH_DURATION seconds (or until the rom list
        // is ready, whichever is later) and is hidden automatically.
        void showSplash();

        // presents the splash on screen for the full minimum duration
        // (SPLASH_DURATION) before any ui/loading dialog is shown, so it reads
        // as a proper boot splash. no-op if no splash is available.
        void presentSplash();

        bool hasSplash() { return pSplash != nullptr; }

        void setConfig(PEMUConfig *cfg);

        void onUpdate() override;

        void updateInputMapping(bool isRomCfg);

        Skin *getSkin() { return pSkin; }

        void setSkin(Skin *s) { pSkin = s; }

        PEMUConfig *getConfig() { return pConfig; }

        UIHighlight *getUiHighlight() { return pHighlight; }

        UIRomList *getUiRomList() { return pRomList; }

        UiEmu *getUiEmu() { return pEmu; }

        UiMenu *getUiMenu() { return pMenu; }

        UiMenuState *getUiStateMenu() { return pState; }

        UiStatusBox *getUiStatusBox() { return pStatusBox; }

        UIProgressBox *getUiProgressBox() { return pProgressBox; }

        c2d::MessageBox *getUiMessageBox() { return pMessageBox; }

        int getFontSize() { return (int) ((float) C2D_DEFAULT_CHAR_SIZE * pSkin->getScaling().y); }

        c2d::Vector2f getScaling() { return pSkin->getScaling(); }

        bool done = false;

    private:
        PEMUConfig *pConfig = nullptr;
        Skin *pSkin = nullptr;
        UIHighlight *pHighlight = nullptr;
        UiMenu *pMenu = nullptr;
        UIRomList *pRomList = nullptr;
        UiEmu *pEmu = nullptr;
        UiMenuState *pState = nullptr;
        UIProgressBox *pProgressBox = nullptr;
        c2d::MessageBox *pMessageBox = nullptr;
        UiStatusBox *pStatusBox = nullptr;
        c2d::RectangleShape *pSplash = nullptr;
        c2d::C2DClock mSplashClock;
        c2d::C2DClock mTimer;
        unsigned int mOldKeys = 0;
    };
}

#endif //GUI_H
