//
// Created by cpasjuste on 22/11/16.
//

#include <algorithm>
#include "pemu.h"
#include "ui_main.h"

UiMain::UiMain(const Vector2f &size) : C2DRenderer(size) {
    printf("UiMain(%ix%i)\n", (int) UiMain::getSize().x, (int) UiMain::getSize().y);
}

void UiMain::init(UIRomList *romList, UiMenu *menu, UiEmu *emu, UiMenuState *state) {
    pRomList = romList;
    pRomList->updateRomList();
    add(pRomList);

    // build menus from options
    pMenu = menu;
    add(pMenu);

    pEmu = emu;
    add(pEmu);

    pState = state;
    add(pState);

    // message box
    Skin::TextGroup textGroup = pSkin->getText(
        {"MAIN", "ROM_LIST", "TEXT"}); // use rom list text size for message box font size
    Skin::RectangleShapeGroup shape = pSkin->getRectangleShape({"SKIN_CONFIG", "MESSAGEBOX"});
    pMessageBox = new c2d::MessageBox(shape.rect, getInput(), pSkin->getFont(), (int) textGroup.size);
    pSkin->loadRectangleShape(pMessageBox, {"SKIN_CONFIG", "MESSAGEBOX"});
    pMessageBox->setSelectedColor(pMessageBox->getFillColor(), pMessageBox->getOutlineColor());
    Color c = pMessageBox->getOutlineColor();
    c.a -= 150;
    pMessageBox->setNotSelectedColor(pMessageBox->getFillColor(), c);
    pMessageBox->setOrigin(Origin::Center);
    if (getSize().x <= 400) {
        pMessageBox->getTitleText()->setOutlineThickness(1);
        pMessageBox->getMessageText()->setOutlineThickness(1);
    }
    add(pMessageBox);

    createProgressBox();

    pStatusBox = new UiStatusBox(this);
    add(pStatusBox);

    // the splash is added very early (before the rom list / progress dialog)
    // so it renders behind the first-scan dialog; re-add it on top of
    // everything until it auto-hides below.
    if (pSplash) {
        remove(pSplash);
        add(pSplash);
    }

    updateInputMapping(false);
    getInput()->setRepeatDelay(INPUT_DELAY);
}

void UiMain::createProgressBox() {
    if (pProgressBox) {
        return;
    }
    pProgressBox = new UIProgressBox(this);
    add(pProgressBox);
}

void UiMain::showSplash() {
    if (pSplash || !pSkin) {
        return;
    }

    // load the skin title texture (title.png). NOTE: must be C2DTexture (the
    // platform class that uploads to the gpu), not the base c2d::Texture which
    // only decodes pixels and stays !available
    C2DTexture *tex = nullptr;
    for (const auto &skinPath: pSkin->getPaths()) {
        std::string texPath = skinPath + "title.png";
        if (getIo()->exist(texPath)) {
            tex = new C2DTexture(texPath);
            if (!tex->available) {
                delete (tex);
                tex = nullptr;
                continue;
            }
            break;
        }
    }
    if (!tex) {
        printf("UiMain::showSplash: title.png not found in skin, no splash\n");
        return;
    }

    // full-screen dark background + title image scaled to SPLASH_SCALE of the
    // screen (aspect preserved, centered)
    pSplash = new c2d::RectangleShape(getSize());
    pSplash->setFillColor(c2d::Color::Black);
    tex->setFilter(c2d::Texture::Filter::Linear);
    tex->setOrigin(c2d::Origin::Center);
    tex->setPosition(getSize().x / 2, getSize().y / 2);
    float scaling = std::min(getSize().x / tex->getSize().x, getSize().y / tex->getSize().y) * SPLASH_SCALE;
    tex->setScale(scaling, scaling);
    tex->setFillColor(c2d::Color::White);
    pSplash->add(tex);
    add(pSplash);
    mSplashClock.restart();
    printf("UiMain::showSplash: title.png (%ix%i), %.0f%% of screen, %.2fs min\n",
           (int) tex->getSize().x, (int) tex->getSize().y, SPLASH_SCALE * 100.f, SPLASH_DURATION);
}

void UiMain::presentSplash() {
    if (!pSplash) {
        return;
    }
    const float splashStart = getElapsedTime().asSeconds();
    while (!done && getElapsedTime().asSeconds() - splashStart < SPLASH_DURATION) {
        // inputs are disabled so button presses during the boot splash are
        // not queued into the rom list
        flip(true, false);
    }
}

void UiMain::onUpdate() {
    // keep the splash on screen for at least SPLASH_DURATION seconds, or
    // until the rom list is ready (during a long first scan), whichever is
    // later; only then hide it. While a scan's blur is still fading in, the
    // splash stays on top so the rom list is not revealed un-blurred for a
    // moment (a bright flash on the very first frames).
    if (pSplash && pSplash->isVisible() && pRomList &&
        mSplashClock.getElapsedTime().asSeconds() >= SPLASH_DURATION) {
        auto *blur = pRomList->getBlur();
        if (!blur || !blur->isVisible() || blur->getAlpha() >= 229) {
            pSplash->setVisibility(Visibility::Hidden);
        }
    }

    unsigned int buttons = getInput()->getButtons();
    if (buttons & Input::Button::Quit) {
        done = true;
        return C2DRenderer::onUpdate();
    }

    if (pEmu && (!pEmu->isVisible() || pEmu->isPaused())) {
        if (buttons != Input::Button::Delay) {
            bool changed = (mOldKeys ^ buttons) != 0;
            mOldKeys = buttons;
            if (!changed) {
                if (mTimer.getElapsedTime().asSeconds() > 5) {
                    getInput()->setRepeatDelay(INPUT_DELAY / 20);
                } else if (mTimer.getElapsedTime().asSeconds() > 3) {
                    getInput()->setRepeatDelay(INPUT_DELAY / 8);
                } else if (mTimer.getElapsedTime().asSeconds() > 1) {
                    getInput()->setRepeatDelay(INPUT_DELAY / 4);
                }
            } else {
                getInput()->setRepeatDelay(INPUT_DELAY);
                mTimer.restart();
            }
        }
    }

    C2DRenderer::onUpdate();
}

void UiMain::setConfig(PEMUConfig *cfg) {
    pConfig = cfg;

    // set fullscreen if requested by config
    const auto fs = pConfig->get(PEMUConfig::OptId::UI_FULLSCREEN);
    if (fs && fs->getInteger()) {
        setFullscreen(fs);
    }
}

void UiMain::updateInputMapping(bool isRomConfig) {
    if (isRomConfig) {
#ifndef NO_KEYBOARD
        getInput()->setKeyboardMapping(pConfig->getKeyboardMapping(0, true));
#endif
        const int dz = pConfig->getJoystickDeadZone(0, true);
        std::vector<Input::ButtonMapping> joyMapping = pConfig->getJoystickMapping(0, true);
        const Vector2i leftAxisMapping = pConfig->getJoystickAxisLeftMapping(0, true);
        const Vector2i rightAxisMapping = pConfig->getJoystickAxisRightMapping(0, true);
        for (int i = 0; i < PLAYER_MAX; i++) {
            getInput()->setJoystickMapping(i, joyMapping, leftAxisMapping, rightAxisMapping, dz);
        }
    } else {
#ifndef NO_KEYBOARD
        std::vector<Input::ButtonMapping> keyMapping = getInput()->getKeyboardMappingDefault();
        // keep custom config for menus keys
        for (unsigned int i = 0; i < keyMapping.size(); i++) {
            if (keyMapping.at(i).button == Input::Button::Menu1) {
                keyMapping.at(i).value = pConfig->get(PEMUConfig::OptId::KEY_MENU1, false)->getInteger();
            } else if (keyMapping.at(i).button == Input::Button::Menu2) {
                keyMapping.at(i).value = pConfig->get(PEMUConfig::OptId::KEY_MENU2, false)->getInteger();
            }
        }
        getInput()->setKeyboardMapping(keyMapping);
#endif
        std::vector<Input::ButtonMapping> joyMapping = getInput()->getPlayer(0)->mapping_default;
        // keep custom config for menus keys
        for (unsigned int i = 0; i < joyMapping.size(); i++) {
            if (joyMapping.at(i).button == Input::Button::Menu1) {
                joyMapping.at(i).value = pConfig->get(PEMUConfig::OptId::JOY_MENU1, false)->getInteger();
            } else if (joyMapping.at(i).button == Input::Button::Menu2) {
                joyMapping.at(i).value = pConfig->get(PEMUConfig::OptId::JOY_MENU2, false)->getInteger();
            }
        }
        for (int i = 0; i < PLAYER_MAX; i++) {
            getInput()->setJoystickMapping(i, joyMapping,
                                           {KEY_JOY_AXIS_LX, KEY_JOY_AXIS_LY},
                                           {KEY_JOY_AXIS_RX, KEY_JOY_AXIS_RY},
                                           pConfig->getJoystickDeadZone(0, false));
        }
    }
}
