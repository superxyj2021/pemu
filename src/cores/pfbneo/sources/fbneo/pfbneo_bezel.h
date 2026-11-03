//
// pfbneo bezel (遮罩/边框) support.
//
// Port of the FBNeo Windows bezel feature (scrn.cpp) following the
// "data driven placement + composited rendering" idea from RetroArch:
//   - a bezel PNG is drawn fullscreen BEHIND the game video,
//   - an optional <drvname>.json config places the game screen inside the
//     bezel's hollow area via a normalized "viewport" rectangle.
//
// File layout (under the data path):
//   bezels/<drvname>.png            per-driver bezel
//   bezels/<parent>.png             parent (clone of) driver fallback
//   bezels/<system>.png             hardware-class bezel (<system>_v for
//                                   vertical games; same names as FBNeo)
//   bezels/<drvname>.json           optional config:
//       { "viewport": [x, y, w, h], "rect": [x, y, w, h] }
//   both normalized [0..1] in screen space. "viewport" = the rectangle the
//   game image is contained inside (default: the existing center-contain
//   logic, which fits classic symmetric bezels); "rect" = where the bezel
//   image itself is drawn (default: fullscreen).
//
// No .png found for the game -> no bezel (the feature stays hidden/off).
//

#ifndef PFBNEO_BEZEL_H
#define PFBNEO_BEZEL_H

#include <string>

namespace c2d {
    class Texture;
}

namespace pfbneo_bezel {

    // --- lifecycle ---------------------------------------------------------
    void init();

    // look up the bezel for a game (drvName -> parentName -> system, with the
    // "_v" vertical variant for hardware-class bezels) and load its texture +
    // config. returns true when a bezel is shown.
    bool load(const std::string &drvName, const std::string &parentName,
              unsigned int hw, bool vertical);

    // free the loaded bezel (game stop / disabled)
    void unload();

    bool isActive();
    c2d::Texture *getSprite();          // owned by the module; add to the ui tree

    // normalized game-screen viewport (screen space, [0..1]); true when the
    // json config provided one (otherwise the caller uses its own contain)
    bool getViewport(float &x, float &y, float &w, float &h);

    // normalized bezel-image rect (default 0,0,1,1 = fullscreen)
    void getRect(float &x, float &y, float &w, float &h);

} // namespace pfbneo_bezel

#endif // PFBNEO_BEZEL_H