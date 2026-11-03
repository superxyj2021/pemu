//
// pfbneo bezel (遮罩/边框) module implementation (see pfbneo_bezel.h).
//

#include "skeleton/pemu.h"
#include "pfbneo_bezel.h"
#include "burner.h"
#include "burnint.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace pfbneo_bezel {

static c2d::Texture *s_sprite = nullptr;
static bool s_active = false;
static bool s_hasViewport = false;
static float s_viewport[4] = {0.f, 0.f, 1.f, 1.f};
static float s_rect[4] = {0.f, 0.f, 1.f, 1.f};

static std::string dataPath() {
    if (c2d_renderer && c2d_renderer->getIo()) {
        std::string p = c2d_renderer->getIo()->getDataPath();
        return p.empty() ? "./" : p;
    }
    return "./";
}

// hardware -> system bezel name, same table as the FBNeo frontend
// (scrn.cpp scrn_gamehw_cfg / ScrnGetHWString); "arcade" is the fallback.
static const char *systemName(unsigned int hw) {
    unsigned int pub = hw & HARDWARE_PUBLIC_MASK;
    switch (pub) {
        case HARDWARE_SEGA_MEGADRIVE: return "megadrive";
        case HARDWARE_PCENGINE_PCENGINE: return "pce";
        case HARDWARE_PCENGINE_TG16: return "tg16";
        case HARDWARE_PCENGINE_SGX: return "sgx";
        case HARDWARE_SEGA_SG1000: return "sg1000";
        case HARDWARE_COLECO: return "coleco";
        case HARDWARE_SEGA_MASTER_SYSTEM: return "sms";
        case HARDWARE_SEGA_GAME_GEAR: return "gamegear";
        case HARDWARE_MSX: return "msx";
        case HARDWARE_SPECTRUM: return "spectrum";
        case HARDWARE_NES: return "nes";
        case HARDWARE_FDS: return "fds";
        case HARDWARE_SNES: return "snes";
        case HARDWARE_GBA: return "gba";
        case HARDWARE_SNK_NGP: return "ngp";
        case HARDWARE_SNK_NEOCD: return "neogeocd";
        case HARDWARE_CAPCOM_CPS1: return "cps1";
        case HARDWARE_CAPCOM_CPS2: return "cps2";
        case HARDWARE_CAPCOM_CPS3: return "cps3";
        case HARDWARE_IGS_PGM: return "pgm";
        case HARDWARE_IGS_PGM2: return "pgm2";
        case HARDWARE_SNK_NEOGEO:
        case HARDWARE_SNK_MVS:
            return "neogeo";
        default:
            break;
    }
    return "arcade";
}

static bool fileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// minimal tolerant parser for the bezel json config:
//   { "viewport": [x, y, w, h], "rect": [x, y, w, h] }
static void parseConfig(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rt");
    if (!f) {
        return;
    }
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    auto parseArray = [&buf](const char *key, float out[4]) -> bool {
        const char *p = strstr(buf, key);
        if (!p) {
            return false;
        }
        p = strchr(p, '[');
        if (!p) {
            return false;
        }
        p++;
        for (int i = 0; i < 4; i++) {
            while (*p && (*p == ' ' || *p == ',' || *p == '\t' || *p == ']')) p++;
            out[i] = (float) strtod(p, (char **) &p);
        }
        return true;
    };

    s_hasViewport = parseArray("viewport", s_viewport);
    if (parseArray("rect", s_rect)) {
        if (s_rect[2] <= 0.f) s_rect[2] = 1.f;
        if (s_rect[3] <= 0.f) s_rect[3] = 1.f;
    }
}

void init() {
    unload();
    s_hasViewport = false;
    s_viewport[0] = s_viewport[1] = 0.f;
    s_viewport[2] = s_viewport[3] = 1.f;
    s_rect[0] = s_rect[1] = 0.f;
    s_rect[2] = s_rect[3] = 1.f;
}

bool load(const std::string &drvName, const std::string &parentName,
          unsigned int hw, bool vertical) {
    unload();

    std::string dir = dataPath() + "bezels/";

    // per-driver, then parent, then hardware-class (with _v for vertical)
    std::string candidates[3];
    candidates[0] = dir + drvName + ".png";
    if (!parentName.empty() && parentName != drvName) {
        candidates[1] = dir + parentName + ".png";
    }
    const char *sys = systemName(hw);
    candidates[2] = dir + std::string(sys) + (vertical ? "_v.png" : ".png");

    std::string found;
    printf("pfbneo_bezel: load '%s' (parent '%s', hw 0x%X, vertical %d), dir: %s\n",
           drvName.c_str(), parentName.c_str(), hw, vertical ? 1 : 0, dir.c_str());
    for (const auto &c : candidates) {
        if (c.empty()) continue;
        printf("pfbneo_bezel:   try: %s -> %s\n", c.c_str(), fileExists(c) ? "FOUND" : "not found");
        if (fileExists(c)) {
            found = c;
            break;
        }
    }
    if (found.empty()) {
        printf("pfbneo_bezel:   no bezel image found, disabled\n");
        return false;
    }

    // optional config: bezels/<drvname>.json (falls back to parent/system name)
    s_hasViewport = false;
    for (const auto &c : candidates) {
        if (c.empty()) continue;
        std::string cfgPath = c.substr(0, c.rfind('.')) + ".json";
        if (fileExists(cfgPath)) {
            printf("pfbneo_bezel:   config: %s\n", cfgPath.c_str());
            parseConfig(cfgPath);
            break;
        }
    }

    // C2DTexture (platform class) uploads to the gpu and sets available;
    // the base c2d::Texture only decodes pixels and stays !available
    s_sprite = new C2DTexture(found);
    if (!s_sprite || !s_sprite->available) {
        printf("pfbneo_bezel:   texture load failed for %s\n", found.c_str());
        delete s_sprite;
        s_sprite = nullptr;
        s_hasViewport = false;
        return false;
    }
    s_active = true;
    printf("pfbneo_bezel: loaded %s (viewport %d, size %ix%i)\n", found.c_str(),
           s_hasViewport ? 1 : 0, s_sprite->getTextureSize().x, s_sprite->getTextureSize().y);
    return true;
}

void unload() {
    delete s_sprite;
    s_sprite = nullptr;
    s_active = false;
    s_hasViewport = false;
    s_viewport[0] = s_viewport[1] = 0.f;
    s_viewport[2] = s_viewport[3] = 1.f;
    s_rect[0] = s_rect[1] = 0.f;
    s_rect[2] = s_rect[3] = 1.f;
}

bool isActive() {
    return s_active && s_sprite != nullptr;
}

c2d::Texture *getSprite() {
    return s_sprite;
}

bool getViewport(float &x, float &y, float &w, float &h) {
    if (!s_hasViewport) {
        return false;
    }
    x = s_viewport[0];
    y = s_viewport[1];
    w = s_viewport[2];
    h = s_viewport[3];
    return true;
}

void getRect(float &x, float &y, float &w, float &h) {
    x = s_rect[0];
    y = s_rect[1];
    w = s_rect[2];
    h = s_rect[3];
}

} // namespace pfbneo_bezel