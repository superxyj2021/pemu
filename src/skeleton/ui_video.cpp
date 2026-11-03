//
// Created by cpasjuste on 01/12/16.
//

#include "pemu.h"

C2DUIVideo::C2DUIVideo(UiMain *u, uint8_t **_pixels, int *_pitch,
                       const c2d::Vector2i &size, const c2d::Vector2i &a, Texture::Format format)
    : C2DTexture(size, format) {
    ui = u;
    aspect = a;

    if (_pixels != nullptr) {
        C2DTexture::lock(_pixels, _pitch, getTextureRect());
        C2DTexture::unlock();
    }
}

void C2DUIVideo::updateScaling(const bool vertical, const bool flip, const c2d::FloatRect *target) {
    bool rotated = false;
    float rotation = 0;
    int rotation_cfg = 0;
    std::string scale_value = ui->getConfig()->get(PEMUConfig::OptId::EMU_SCALING, true)->getString();
    float scale_value_float = (float) ui->getConfig()->get(PEMUConfig::OptId::EMU_SCALING, true)->getArrayIndex() + 1;
    std::string scaling_mode = ui->getConfig()->get(PEMUConfig::OptId::EMU_SCALING_MODE, true)->getString();
    float game_aspect_ratio = vertical ? (float) aspect.y / (float) aspect.x : (float) aspect.x / (float) aspect.y;
    c2d::config::Option *rotationOpt = ui->getConfig()->get(PEMUConfig::OptId::EMU_ROTATION, true);
    if (rotationOpt) {
        rotation_cfg = rotationOpt->getArrayIndex();
    }

    Vector2f screen = ui->getSize();
    // bezel viewport: contain the game inside the target rect instead of the
    // whole screen; the rect is in screen pixel coordinates
    Vector2f targetPos = {screen.x / 2.f, screen.y / 2.f};
    Vector2f targetSize = screen;
    if (target) {
        targetPos = {target->left + target->width / 2.f, target->top + target->height / 2.f};
        targetSize = {target->width, target->height};
    }
    Vector2f scale_max;
    Vector2f scale;

    if (vertical) {
        switch (rotation_cfg) {
            case 1: // ON
                rotation = flip ? 90 : 270;
                rotated = true;
                break;
            case 2: // FLIP
            case 3: // CAB
                rotation = flip ? 0 : 180;
                break;
            default: // OFF
                rotation = flip ? 180 : 0;
                break;
        }
    } else if (flip) {
        rotation = 180;
    }

    if (rotated) {
        scale_max.x = targetSize.x / (float) getTextureRect().height;
        scale_max.y = targetSize.y / (float) getTextureRect().width;
    } else {
        scale_max.x = targetSize.x / (float) getTextureRect().width;
        scale_max.y = targetSize.y / (float) getTextureRect().height;
    }

    if (scale_value == TEXT_SCALING_FULL) {
        scale.x = rotated ? scale_max.y : scale_max.x;
        scale.y = rotated ? scale_max.x : scale_max.y;
    } else {
        if (!rotated) {
            scale.y = scale_value == TEXT_SCALING_FIT ? scale_max.y : std::min(scale_max.y, scale_value_float);
            float size_x = ((float) getTextureRect().height * scale.y) * game_aspect_ratio;
            if (scaling_mode == TEXT_SCALING_MODE_AUTO) {
                scale.x = size_x / (float) getTextureRect().width;
                // use integer scaling if aspect ratio is not too divergent
                if (scale.y / scale.x < 1.2f && scale.y / scale.x > 0.8f) {
                    scale.x = scale.y;
                }
            } else if (scaling_mode == TEXT_SCALING_MODE_ASPECT) {
                scale.x = size_x / (float) getTextureRect().width;
            } else {
                // integer scaling
                scale.x = scale.y;
            }
        } else {
            scale.x = scale_value == TEXT_SCALING_FIT ? scale_max.y : std::min(scale_max.y, scale_value_float);
            float size_y = ((float) getTextureRect().width * scale.x) / game_aspect_ratio;
            if (scaling_mode == TEXT_SCALING_MODE_AUTO) {
                scale.y = size_y / (float) getTextureRect().height;
                // use integer scaling if aspect ratio is not too divergent
                if (scale.y / scale.x < 1.2f && scale.y / scale.x > 0.8f) {
                    scale.y = scale.x;
                }
            } else if (scaling_mode == TEXT_SCALING_MODE_ASPECT) {
                scale.y = size_y / (float) getTextureRect().height;
            } else {
                // integer scaling
                scale.y = scale.x;
            }
        }
    }

    printf("C2DUIVideo::updateScaling: mode: %s, scaling: %f x %f,"
           " size: %i x %i, vertical: %i, flipped: %i, rotation: %i\n",
           scale_value.c_str(), scale.x, scale.y,
           (int) ((float) getTextureRect().width * scale.x), (int) ((float) getTextureRect().height * scale.y),
           vertical, flip, rotation_cfg);

    setOrigin(Origin::Center);
    setPosition(targetPos.x, targetPos.y);
    setScale(scale);
    setRotation(rotation);
}
