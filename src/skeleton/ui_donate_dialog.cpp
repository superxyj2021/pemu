//
// pfbneo donate (打赏作者) dialog implementation.
//

#include "ui_donate_dialog.h"
#include "pemu.h"
#include "buffer.h"

#include <cstdio>
#include <algorithm>

using namespace c2d;
using namespace pemu;

// dialog geometry: 850 wide (wide enough for the full intro line), screen-
// centered both ways, height = 85% of the screen height.
#define DIALOG_WIDTH   (850.0f)
// side padding inside the dialog: content (text and image) keeps this much
// clearance from the left/right edges.
#define PAD_X          (32.0f)
// the image is scaled to fill the content width (dialog width minus both side
// paddings), preserving the aspect ratio.
#define IMG_MAX_W      (DIALOG_WIDTH - 2 * PAD_X)
// text is a bit larger than the plain menu items (kept moderate so the whole
// 30-char intro line still fits on one line)
#define TEXT_SIZE_ADD  (2u)

DonateDialog::DonateDialog(UiMain *main) : SkinnedRectangle(main, {"STATES_MENU"}) {
    ui = main;

    setSize(DIALOG_WIDTH, ui->getSize().y * 0.85f);

    // title ("打赏作者")
    title = new SkinnedText(ui, {"STATES_MENU", "TITLE_TEXT"});
    title->setOrigin(Origin::Center);
    title->setPosition(getSize().x / 2, 28);
    title->setString(TEXT_MONEY2);
    SkinnedRectangle::add(title);

    // same text setup as the menu items / cheat rows, but slightly larger
    auto textGroup = ui->getSkin()->getText({"OPTIONS_MENU", "ITEMS_TEXT"});
    Font *font = ui->getSkin()->getFont();
    unsigned int textSize = textGroup.size + TEXT_SIZE_ADD;

    // intro line ("汉化和开发新功能不易，...")
    introText = new Text(TEXT_MONEY2_TEXT1, textSize, font);
    introText->setFillColor(textGroup.color);
    introText->setSizeMax(IMG_MAX_W, 0);
    introText->setOrigin(Origin::Center);
    introText->setPosition(getSize().x / 2, 76);
    SkinnedRectangle::add(introText);

    // donate QR image: romfs:/donate/donate.png first, embedded buffer_img
    // as fallback (so the dialog still shows something if the file is gone)
    donateImage = nullptr;
    if (ui->getIo()) {
        donateImage = new C2DTexture(ui->getIo()->getRomFsPath() + "donate/donate.png");
        if (!donateImage->available) {
            delete donateImage;
            donateImage = nullptr;
        }
    }
    if (donateImage == nullptr) {
        donateImage = new C2DTexture(buffer_img, BUFFER_IMG_SIZE);
    }

    // version line pinned to the bottom of the dialog (with a margin).
    // the image lives between the intro line and the version line.
    const float versionY = getSize().y - 32.f;

    if (donateImage->available) {
        float w = (float) donateImage->getTextureRect().width;
        float h = (float) donateImage->getTextureRect().height;
        // as wide as the content area allows; height is capped to the space
        // between the intro line and the version line (aspect preserved).
        float maxH = (versionY - 48.f) - 96.f;
        if (maxH < 40.f) {
            maxH = 40.f;
        }
        float scale = std::min(IMG_MAX_W / w, maxH / h);
        donateImage->setScale(scale, scale);
        donateImage->setOrigin(Origin::Center);
        donateImage->setPosition(getSize().x / 2, 96.f + maxH / 2.f);
        SkinnedRectangle::add(donateImage);
        m_imageBottom = donateImage->getPosition().y + (h * scale) / 2.f;
    } else {
        m_imageBottom = 96.f + 60.f;
    }

    // version line ("FBNeo Plus 版本: ...")
    std::string versionStr = std::string(TEXT_MONEY2_VERSION) + TEXT_MONEY2_VERSION_DEFAULT;
    versionText = new Text(versionStr, textSize, font);
    versionText->setFillColor(textGroup.color);
    versionText->setSizeMax(IMG_MAX_W, 0);
    versionText->setOrigin(Origin::Center);
    versionText->setPosition(getSize().x / 2, versionY);
    SkinnedRectangle::add(versionText);

    // hidden by default
    SkinnedRectangle::setVisibility(Visibility::Hidden);
}

DonateDialog::~DonateDialog() {
    // children are deleted by the c2d tree
}

void DonateDialog::open() {
    // screen-centered both ways: the STATES_MENU skin rect is offscreen, so
    // we re-position the box to the middle of the screen.
    setOrigin(Origin::TopLeft);
    setPosition((ui->getSize().x - getSize().x) / 2,
                (ui->getSize().y - getSize().y) / 2);
    setVisibility(Visibility::Visible, true);
    // keep the same blur mask as when the menu is open (main screen case: the
    // rom list stays visible behind the dialog)
    if (ui->getUiRomList() && ui->getUiRomList()->isVisible()) {
        ui->getUiRomList()->getBlur()->setVisibility(Visibility::Visible, true);
    }
}

void DonateDialog::close() {
    setVisibility(Visibility::Hidden, true);
}

bool DonateDialog::onInput(c2d::Input::Player *players) {
    unsigned int buttons = players[0].buttons;
    if (buttons & Input::Button::B) {
        close();
        // mark the "just closed" state: UiMenu::onInput forwards input while
        // the dialog is visible, but the menu may still see the same B press
        // right after the dialog closes and would hide itself again. the flag
        // is cleared on the next update frame (see onUpdate).
        m_justClosed = true;
        // close behaviour: always bring the menu back (it was hidden when the
        // dialog opened). if a game is running it stays paused in the rom
        // menu; the user exits the menu (B) to resume the game.
        if (ui->getUiMenu()) {
            ui->getUiMenu()->setVisibility(Visibility::Visible, true);
        }
    }
    return true;
}

void DonateDialog::onUpdate() {
    m_justClosed = false;
    SkinnedRectangle::onUpdate();
}

void DonateDialog::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    SkinnedRectangle::setVisibility(visibility, tweenPlay);
    if (visibility == Visibility::Hidden && tweenPlay) {
        // nothing to clean up here, children move with the box
    }
}
