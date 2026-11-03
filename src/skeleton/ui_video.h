//
// Created by cpasjuste on 25/11/16.
//

#ifndef C2DUI_VIDEO_H_
#define C2DUI_VIDEO_H_

namespace pemu {

    class C2DUIVideo : public c2d::C2DTexture {

    public:

        C2DUIVideo(UiMain *ui, uint8_t **pixels, int *pitch, const c2d::Vector2i &size,
                   const c2d::Vector2i &aspect = {4, 3}, c2d::Texture::Format format = c2d::Texture::Format::RGB565);

        // target (optional): contain the game inside this pixel rect instead of the
// full screen (used by the bezel viewport); nullptr = full screen.
        virtual void updateScaling(bool vertical = false, bool flip = false,
                                   const c2d::FloatRect *target = nullptr);

        c2d::Vector2i aspect;

        UiMain *getUi() { return ui; }

    private:

        UiMain *ui = nullptr;
    };
}

#endif //C2DUI_VIDEO_H_
