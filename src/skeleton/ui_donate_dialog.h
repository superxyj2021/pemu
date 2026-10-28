//
// pfbneo donate (打赏作者) dialog.
//
// opened from the main/rom menu ("打赏作者" item): shows the intro line,
// the donate QR code image (romfs:/donate/donate.png, falling back to the
// embedded buffer_img) and the FBNeo Plus version line, styled like the
// cheat dialog (STATES_MENU skin, centered 640-wide box at y=180).
//

#ifndef PEMU_UI_DONATE_DIALOG_H
#define PEMU_UI_DONATE_DIALOG_H

// like pfbneo_ui_cheat.h, go through pemu.h: it pulls in cross2d/c2d.h (for
// the c2d namespace used by the skin classes) and the pemu namespace (UiMain,
// Skin) that SkinnedRectangle/SkinnedText refer to.
#include "pemu.h"
#include "skin/SkinnedRectangle.h"
#include "skin/SkinnedText.h"

namespace pemu {

    class UiMain;

    class DonateDialog : public SkinnedRectangle {

    public:
        explicit DonateDialog(UiMain *main);

        ~DonateDialog() override;

        void open();

        void close();

        bool onInput(c2d::Input::Player *players) override;

        void onUpdate() override;

        void setVisibility(c2d::Visibility visibility, bool tweenPlay = false) override;

        // true for one frame after B closed the dialog (lets UiMenu::onInput
        // swallow the same B press so it does not hide the menu right away)
        bool justClosed() const { return m_justClosed; }

    private:
        UiMain *ui = nullptr;
        SkinnedText *title = nullptr;
        c2d::Text *introText = nullptr;
        c2d::C2DTexture *donateImage = nullptr;
        c2d::Text *versionText = nullptr;
        float m_imageBottom = 0.f;
        bool m_justClosed = false;
    };
}

#endif //PEMU_UI_DONATE_DIALOG_H
