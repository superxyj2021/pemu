//
// pfbneo turbo (auto-fire / 连发) support module implementation.
//
// See pfbneo_turbo.h for the design notes.
//
// Turbo rate levels (frames are 1/60s in pfbneo):
//   index | period | on | off | presses/sec (approx)
//      0  |   16   |  2 | 14  | ~4Hz   (slowest)
//      1  |   12   |  2 | 10  | ~5Hz
//      2  |    8   |  2 |  6  | ~8Hz
//      3  |    6   |  2 |  4  | ~10Hz
//      4  |    4   |  2 |  2  | ~15Hz  (default)
//      5  |    2   |  1 |  1  | ~30Hz  (fastest)
//

// NOTE on include order:
//  "skeleton/pemu.h" MUST come first: it is the only header that pulls in
//  ss_api.h, cross2d/c2d.h, skeleton/pemu_config.h, skeleton/ui_text_def.h
//  AND the "using namespace ss_api" that pfbneo_utility.h relies on
//  (pfbneo_utility.h uses the unqualified type "Game").

#include "skeleton/pemu.h"
#include "pfbneo_turbo.h"
#include "pfbneo_utility.h"
#include "burner.h"
#include "retro_input_wrapper.h"

using namespace c2d;

namespace pfbneo_turbo {

    // ---------------------------------------------------------------------------
    // static data
    // ---------------------------------------------------------------------------

    // retro joystick ids of the 8 turbo-able buttons, in menu order
    static const unsigned int s_slotIds[TURBO_BUTTON_COUNT] = {
        RETRO_DEVICE_ID_JOYPAD_A,   // 0: A
        RETRO_DEVICE_ID_JOYPAD_B,   // 1: B
        RETRO_DEVICE_ID_JOYPAD_X,   // 2: X
        RETRO_DEVICE_ID_JOYPAD_Y,   // 3: Y
        RETRO_DEVICE_ID_JOYPAD_L,   // 4: LT
        RETRO_DEVICE_ID_JOYPAD_R,   // 5: RT
        RETRO_DEVICE_ID_JOYPAD_L2,  // 6: LB
        RETRO_DEVICE_ID_JOYPAD_R2   // 7: RB
    };

    static const char *s_slotNames[TURBO_BUTTON_COUNT] = {
        "TURBO_A", "TURBO_B", "TURBO_X", "TURBO_Y",
        "TURBO_LT", "TURBO_RT", "TURBO_LB", "TURBO_RB"
    };

    // display names come from ui_text_def.h (Chinese on switch builds)
    static const char *s_slotDisplayNames[TURBO_BUTTON_COUNT] = {
        TEXT_MENU_TURBO_A, TEXT_MENU_TURBO_B, TEXT_MENU_TURBO_X, TEXT_MENU_TURBO_Y,
        TEXT_MENU_TURBO_LT, TEXT_MENU_TURBO_RT, TEXT_MENU_TURBO_LB, TEXT_MENU_TURBO_RB
    };

    // 6 rate levels: [period_frames, on_frames]
    static const int s_ratePeriod[TURBO_RATE_COUNT] = { 16, 12, 8, 6, 4, 2 };
    static const int s_rateOnFrames[TURBO_RATE_COUNT] = { 2, 2, 2, 2, 2, 1 };
    static const char *s_rateNames[TURBO_RATE_COUNT] = {
        "4Hz", "5Hz", "8Hz", "10Hz", "15Hz", "30Hz"
    };
    static const int s_rateDefaultIndex = 2; // 15Hz

    static c2d::config::Config *s_gameConfig = nullptr;

    // current runtime state
    static unsigned int s_turboFrameCounter = 0;   // global frame counter
    static unsigned int s_holdCounter[TURBO_BUTTON_COUNT] = { 0 };
    static unsigned int s_enabledMask = 0;         // bit i set -> button i turbo on
    static int s_rateIndex = s_rateDefaultIndex;
    static int s_lastRateIndex = s_rateDefaultIndex;
    static unsigned int s_supportedMask = 0xFF;    // buttons used by the current rom

    static FILE *s_logFile = nullptr;

    // ---------------------------------------------------------------------------
    // logging (writes to /switch/turbo_debug.log on the sd card)
    // controlled by the master switch PEMU_ENABLE_DEBUG_LOG (pemu.h), OFF by default
    // ---------------------------------------------------------------------------
#if PEMU_ENABLE_DEBUG_LOG
    void turboLog(const char *fmt, ...) {
        va_list args;

        if (!s_logFile) {
            s_logFile = fopen("/switch/turbo_debug.log", "a");
        }
        if (s_logFile) {
            va_start(args, fmt);
            vfprintf(s_logFile, fmt, args);
            va_end(args);
            fputc('\n', s_logFile);
            fflush(s_logFile);
        }

        // mirror to stdout for non-switch builds (easier to debug on linux...)
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        printf("\n");
        fflush(stdout);
    }
#else
    void turboLog(const char *fmt, ...) { }
#endif

    // ---------------------------------------------------------------------------
    // helpers
    // ---------------------------------------------------------------------------
    int idToSlot(unsigned int id) {
        for (int i = 0; i < TURBO_BUTTON_COUNT; i++) {
            if (s_slotIds[i] == id) {
                return i;
            }
        }
        return -1;
    }

    unsigned int getButtonId(TurboButton b) {
        if (b < 0 || b >= TURBO_BUTTON_COUNT) {
            return RETRO_DEVICE_ID_JOYPAD_A;
        }
        return s_slotIds[b];
    }

    const char *getButtonName(TurboButton b) {
        if (b < 0 || b >= TURBO_BUTTON_COUNT) {
            return "TURBO_?";
        }
        return s_slotNames[b];
    }

    const char *getButtonDisplayName(TurboButton b) {
        if (b < 0 || b >= TURBO_BUTTON_COUNT) {
            return "TURBO_?";
        }
        return s_slotDisplayNames[b];
    }

    int getRateIndex() {
        return s_rateIndex;
    }

    const char *getRateName(int index) {
        if (index < 0 || index >= TURBO_RATE_COUNT) {
            return "?";
        }
        return s_rateNames[index];
    }

    unsigned int getSupportedMask() {
        return s_supportedMask;
    }

    bool isSupported(TurboButton b) {
        if (b < 0 || b >= TURBO_BUTTON_COUNT) {
            return false;
        }
        return (s_supportedMask & (1u << b)) != 0;
    }

    static void resetCounters() {
        s_turboFrameCounter = 0;
        for (int i = 0; i < TURBO_BUTTON_COUNT; i++) {
            s_holdCounter[i] = 0;
        }
    }

    // ---------------------------------------------------------------------------
    // lifecycle
    // ---------------------------------------------------------------------------
    void init() {
        TURBO_LOG("=== pfbneo turbo init (v1.0) ===");
        TURBO_LOG("log file: /switch/turbo_debug.log");
        resetCounters();
    }

    void onGameStart() {
        TURBO_LOG("=== pfbneo turbo: game start ===");
        TURBO_LOG("  driver: %s", BurnDrvGetTextA(DRV_NAME));
        TURBO_LOG("  hardware: 0x%08X", BurnDrvGetHardwareCode());
        TURBO_LOG("  supported buttons mask: 0x%02X", s_supportedMask);
        TURBO_LOG("  rate: %s, enabled mask: 0x%02X", getRateName(s_rateIndex), s_enabledMask);
        resetCounters();
    }

    void onGameStop() {
        TURBO_LOG("=== pfbneo turbo: game stop ===");
        TURBO_LOG("  final rate: %s, enabled mask: 0x%02X", getRateName(s_rateIndex), s_enabledMask);
        s_gameConfig = nullptr;
        s_enabledMask = 0;
        s_supportedMask = 0xFF;
        resetCounters();
    }

    // ---------------------------------------------------------------------------
    // determine which of the 8 turbo buttons the current rom actually uses.
    //
    // 1) if the requested rom is already the active/mapped driver (in-game menu),
    //    scan the existing mapping table -> exact result, no side effects.
    // 2) otherwise (rom list menu, game not started), activate the driver and
    //    run the real input mapping once (same code path as InputInit), then scan.
    // 3) on failure, fall back to "all 8 buttons".
    // ---------------------------------------------------------------------------
    static int getSupportedMaskForGame(const ss_api::Game &game) {
        int oldActive = nBurnDrvActive;
        unsigned int mask = 0;

        // activate the driver (same helper used by the emulator on game load)
        PFBNEOUtility::setDriverActive(game);
        if (nBurnDrvActive >= nBurnDrvCount) {
            TURBO_LOG("pfbneo turbo: probe: driver not found for %s, using all buttons",
                      c2d::Utility::removeExt(game.path).c_str());
            return 0xFF;
        }

        bool sameDriver = (oldActive == (int) nBurnDrvActive);

        if (sameDriver && PfbneoTurboInputInitialized()) {
            // game is running (or was mapped already): scan the current mapping
            mask = PfbneoTurboGetUsedMask(false);
        }

        if (mask == 0) {
            if (!sameDriver && PfbneoTurboInputInitialized()) {
                // requesting a different rom while another one is running:
                // this should not happen through the UI, do not touch the
                // running mapping, just show all buttons
                TURBO_LOG("pfbneo turbo: probe: warning, probing a different rom while "
                          "a game is running, showing all buttons");
                return 0xFF;
            }
            // rom list case (no game running): run the real mapping once
            mask = PfbneoTurboGetUsedMask(true);
        }

        TURBO_LOG("pfbneo turbo: probe: rom=%s driver=%s mask=0x%02X",
                  c2d::Utility::removeExt(game.path).c_str(), BurnDrvGetTextA(DRV_NAME), mask);
        return (int) mask;
    }

    // ---------------------------------------------------------------------------
    // per-rom config group creation.
    // called by PEMUConfig::loadGame() (via PFBAConfig::onGameConfigCreate)
    // BEFORE the per-rom config file is loaded, so that persisted turbo values
    // are read back into the option objects.
    // ---------------------------------------------------------------------------
    void onGameConfigCreate(const ss_api::Game &game, c2d::config::Config *gameConfig) {
        s_gameConfig = gameConfig;

        TURBO_LOG("=== pfbneo turbo: per-rom config ===");
        TURBO_LOG("  rom: %s", game.path.c_str());

        int mask = getSupportedMaskForGame(game);
        s_supportedMask = (unsigned int) mask;

        c2d::config::Group turboGroup("TURBO", TURBO_GROUP_ID, TEXT_MENU_TURBO);

        std::vector<std::string> rates;
        for (int i = 0; i < TURBO_RATE_COUNT; i++) {
            rates.emplace_back(s_rateNames[i]);
        }
        turboGroup.addOption({
            "TURBO_RATE", rates, s_rateDefaultIndex, TURBO_OPT_RATE, TEXT_MENU_TURBO_HINT, TEXT_MENU_TURBO_RATE
        });

        for (int i = 0; i < TURBO_BUTTON_COUNT; i++) {
            if (!(mask & (1u << i))) {
                TURBO_LOG("  %s: skipped (not used by this rom)", s_slotNames[i]);
                continue;
            }
            std::vector<std::string> onOff = {"OFF", "ON"};
            turboGroup.addOption({
                s_slotNames[i], onOff, 0, TURBO_OPT_A + i, "", s_slotDisplayNames[i]
            });
            TURBO_LOG("  %s: added", s_slotNames[i]);
        }

        gameConfig->addGroup(turboGroup);

        // values will be loaded from the per-rom cfg right after this call
        TURBO_LOG("  group TURBO added (rate index %d, supported mask 0x%02X)",
                  s_rateDefaultIndex, mask);
    }

    // ---------------------------------------------------------------------------
    // per-frame update: refresh config + advance hold counters.
    // called once per emulated frame from InputMake().
    // ---------------------------------------------------------------------------
    void tick() {
        s_turboFrameCounter++;

        // --- refresh config (applies immediately, i.e. on the next frame) ---
        if (s_gameConfig) {
            c2d::config::Option *rateOpt = s_gameConfig->getOption(TURBO_OPT_RATE);
            if (rateOpt) {
                s_rateIndex = rateOpt->getArrayIndex();
                if (s_rateIndex < 0 || s_rateIndex >= TURBO_RATE_COUNT) {
                    s_rateIndex = s_rateDefaultIndex;
                }
            }
            unsigned int newMask = 0;
            for (int i = 0; i < TURBO_BUTTON_COUNT; i++) {
                c2d::config::Option *opt = s_gameConfig->getOption(TURBO_OPT_A + i);
                if (opt && opt->getInteger()) {
                    newMask |= (1u << i);
                }
            }
            if (newMask != s_enabledMask) {
                TURBO_LOG("tick: enabled mask 0x%02X -> 0x%02X", s_enabledMask, newMask);
                s_enabledMask = newMask;
            }
            if (s_rateIndex != s_lastRateIndex) {
                TURBO_LOG("tick: rate %d -> %d (%s)", s_lastRateIndex, s_rateIndex,
                          getRateName(s_rateIndex));
                s_lastRateIndex = s_rateIndex;
            }
        }

        // --- update per-button hold counters from raw input ------------------
        // any player port / keyboard counts: turbo is global for all devices
        if (c2d_renderer) {
            for (int i = 0; i < TURBO_BUTTON_COUNT; i++) {
                unsigned int id = s_slotIds[i];
                bool held = false;
                for (int p = 0; p < PLAYER_MAX; p++) {
                    Input::Player *player = c2d_renderer->getInput()->getPlayer(p);
                    if (player && player->enabled && (player->buttons & id)) {
                        held = true;
                        break;
                    }
                }
                if (held) {
                    if (s_holdCounter[i] < 0x7FFFFFFFu) {
                        s_holdCounter[i]++;
                    }
                } else {
                    s_holdCounter[i] = 0;
                }
            }
        }
    }

    // ---------------------------------------------------------------------------
    // apply turbo to a joystick button query.
    // called from CinpState() for every mapped digital joystick input.
    //  - rawState == 0 : button not held -> never auto press
    //  - rawState == 1 : button held -> output depends on the turbo pattern
    //  - turbo disabled for this button -> pass-through
    // ---------------------------------------------------------------------------
    int apply(unsigned int id, int rawState) {
        if (rawState == 0) {
            return 0;
        }

        int slot = idToSlot(id);
        if (slot < 0) {
            return rawState;
        }

        if (!(s_enabledMask & (1u << slot))) {
            return rawState;
        }

        if (s_rateIndex < 0 || s_rateIndex >= TURBO_RATE_COUNT) {
            return rawState;
        }

        int period = s_ratePeriod[s_rateIndex];
        int on = s_rateOnFrames[s_rateIndex];
        if (period <= 0 || on <= 0) {
            return rawState;
        }

        // while held, the game sees "on" for the first `on` frames of each
        // `period`-frame cycle
        return ((int) (s_holdCounter[slot] % (unsigned int) period) < on) ? 1 : 0;
    }

} // namespace pfbneo_turbo
