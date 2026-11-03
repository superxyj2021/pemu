//
// Created by cpasjuste on 01/06/18.
//

#ifndef PFBA_UIEMU_H
#define PFBA_UIEMU_H

#include <string>

class PFBAUiEmu : public pemu::UiEmu {

public:

    explicit PFBAUiEmu(pemu::UiMain *ui);

    int load(const ss_api::Game &game) override;

    void stop() override;

    bool onMenuExtra(const std::string &name) override;

    bool isExtraDialogOpen() override;

    // DIP switches exist for the selected game? (arcade AND some home console
    // drivers define them; games without DIP entries hide the menu item)
    bool hasExtraFeature(const std::string &name) override;

    // latch a soft reset (consumed in onUpdate right after InputMake)
    void requestReset() override;

    // mark/take the pending restart action (see base UiEmu): 1 = soft reset
    // (DIP / memory card), 2 = full reload (IPS rom patches).
    void markSettingsAction(int action) override;
    int takeSettingsAction() override;

    // reload the running game (stop + load): applies changed IPS settings.
    // called when the in-game menu closes while a dialog modified a setting.
    void reload() override;

    // attach the cheat dialog to the ui tree (as the LAST object, i.e.
    // rendered/input on top of everything). must be called once, AFTER
    // UiMain::init() (main loop start), exactly like UiMenuState - NOT from
    // a runtime input callback, which the c2d render traversal may skip.
    void attachToUi() override;

private:

#ifdef __PFBA_ARM__
    int getSekCpuCore();
#endif

    bool onInput(c2d::Input::Player *players) override;

    void onUpdate() override;

    class PfbneCheatDialog *cheatDialog = nullptr;
    class PfbneKeyDialog *keyDialog = nullptr;
    class PfbneDipDialog *dipDialog = nullptr;
    class PfbneMemcardDialog *memcardDialog = nullptr;
    class PfbneIpsDialog *ipsDialog = nullptr;
    class PfbneMovelistDialog *movelistDialog = nullptr;
    bool m_dialogAttached = false;

    // latched "reset game" request: set from the rom menu, consumed in
    // onUpdate() right after InputMake() (where the reset switch pulse can
    // reach the driver frame, like the Select-button long-press path)
    bool m_resetRequested = false;

    // pending restart action from a dialog (0 = none, 1 = reset, 2 = reload)
    int m_settingsAction = 0;

    bool audio_sync = false;

    int frameskip = 0;

    // auto frameskip state: moving average of BurnDrvFrame cpu time and the
    // current auto skip level (0/1). reset on game load.
    float m_frameTimeAvg = 0.f;
    int m_autoSlowFrames = 0;
    int m_autoFastFrames = 0;
    int m_autoFrameskip = 0;

    C2DClock clock;
};

#endif //PFBA_UIEMU_H
