//
// Created by cpasjuste on 01/12/16.
//

#include "skeleton/pemu.h"
#include "pfbneo_ui_video.h"
#include "fbneo/pfbneo_bezel.h"
#include "burner.h"

using namespace c2d;
using namespace pemu;

PFBAVideo::PFBAVideo(UiMain *ui, uint8_t **_pixels, int *_pitch, const c2d::Vector2i &size, const c2d::Vector2i &aspect)
        : C2DUIVideo(ui, _pixels, _pitch, size, aspect) {
}

void PFBAVideo::updateScaling(bool vertical, bool flip, const c2d::FloatRect *target) {
    (void) vertical;
    (void) flip;

    bool v = (bool) (BurnDrvGetFlags() & BDF_ORIENTATION_VERTICAL);
    bool f = (bool) (BurnDrvGetFlags() & BDF_ORIENTATION_FLIPPED);

    // position the bezel background sprite (drawn behind the game video)
    if (pfbneo_bezel::isActive()) {
        auto *bezel = pfbneo_bezel::getSprite();
        float bx, by, bw, bh;
        pfbneo_bezel::getRect(bx, by, bw, bh);
        Vector2f screen = getUi()->getSize();
        float texW = (float) bezel->getTextureSize().x;
        float texH = (float) bezel->getTextureSize().y;
        bezel->setOrigin(Origin::TopLeft);
        bezel->setPosition(screen.x * bx, screen.y * by);
        bezel->setScale(screen.x * bw / texW, screen.y * bh / texH);
        bezel->setVisibility(Visibility::Visible);
    }

    // bezel viewport: contain the game inside the hollow area (pixel rect)
    float vx, vy, vw, vh;
    c2d::FloatRect vpRect;
    bool useViewport = pfbneo_bezel::isActive() && pfbneo_bezel::getViewport(vx, vy, vw, vh);
    if (useViewport) {
        Vector2f screen = getUi()->getSize();
        vpRect.left = screen.x * vx;
        vpRect.top = screen.y * vy;
        vpRect.width = screen.x * vw;
        vpRect.height = screen.y * vh;
    }

    C2DUIVideo::updateScaling(v, f, useViewport ? &vpRect : nullptr);
}
