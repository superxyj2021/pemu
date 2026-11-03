//
// Created by cpasjuste on 03/02/18.
//

#ifndef C2DUI_UI_EMU_H
#define C2DUI_UI_EMU_H

namespace pemu {
    class UiEmu : public c2d::RectangleShape {
    public:
        explicit UiEmu(UiMain *ui);

        virtual int load(const ss_api::Game &game);

        virtual void stop();

        virtual void pause();

        virtual void resume();

        // hook for core-specific extra rom-menu entries (e.g. "金手指/cheat).
        // called by UiMenu when such an entry is confirmed; return true if
        // the name was handled by the core.
        virtual bool onMenuExtra(const std::string &name);

        // hook for core-specific extra dialogs (e.g. the cheat dialog).
        // UiMenu / UiEmu::onInput yield input to the dialog while this is
        // true, so the dialog receives B/LEFT/RIGHT/UP/DOWN directly.
        virtual bool isExtraDialogOpen() { return false; }

        // hook for core-specific rom-menu entries that are only meaningful
        // for games that actually have the feature (e.g. DIP switches):
        // the menu entry is hidden when this returns false.
        virtual bool hasExtraFeature(const std::string &name) { (void) name; return false; }

        // called when the "遮罩/边框" (bezel) menu option changes while a game
        // is running; cores that render a bezel reload it live here
        virtual void onBezelChanged() {}

        // request a soft game reset (like the in-game "reset game" entry);
        // used by dialogs whose changes only take effect on a reset.
        virtual void requestReset() {}

        // a dialog changed a setting that needs a game restart: called by the
        // in-game menu when it closes. returns the pending action:
        //   0 = nothing changed (resume normally),
        //   1 = soft reset suffices (e.g. DIP / memory card),
        //   2 = the game must be reloaded (e.g. IPS rom patches).
        // the menu calls requestReset()+resume() for 1 and reload() for 2.
        virtual int takeSettingsAction() { return 0; }

        // mark that a dialog changed a setting needing a restart: action 1 =
        // soft reset, 2 = reload. the higher action wins (reload > reset).
        virtual void markSettingsAction(int action) { (void) action; }

        // reload the running game so changed settings apply (default: resume).
        virtual void reload() { resume(); }

        // attach core-specific dialogs to the ui tree (e.g. the cheat dialog).
        // called once after UiMain::init() from the shared core main; cores
        // without extra dialogs leave the default no-op.
        virtual void attachToUi() {}

        UiMain *getUi();

        C2DUIVideo *getVideo();

        c2d::Audio *getAudio();

        void addAudio(c2d::Audio *audio);

        void addAudio(int rate = 48000, int samples = 2048, c2d::Audio::C2DAudioCallback cb = nullptr);

        void addVideo(C2DUIVideo *video);

        void addVideo(uint8_t **pixels, int *pitch, const c2d::Vector2i &size,
                      const c2d::Vector2i &aspect = {4, 3},
                      c2d::Texture::Format format = c2d::Texture::Format::RGB565);

        c2d::Text *getFpsText();

        bool isPaused();

        bool onInput(c2d::Input::Player *players) override;

        ss_api::Game getCurrentGame() const {
            return currentGame;
        }

        void setExitOnStop(bool value) { mExitOnStop = value; }

        bool getExitOnStop() { return mExitOnStop; }

    protected:
        void onUpdate() override;

        ss_api::Game currentGame;
        c2d::Text *fpsText = nullptr;
        UiMain *pMain = nullptr;
        C2DUIVideo *video = nullptr;
        c2d::Audio *audio = nullptr;
        char fpsString[32];
        float targetFps = 60;
        bool paused = true;
        bool mExitOnStop = false;
    };
}

#endif //C2DUI_UI_EMU_H
