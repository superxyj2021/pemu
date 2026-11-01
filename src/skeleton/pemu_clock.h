#ifndef PEMU_CLOCK_H_
#define PEMU_CLOCK_H_

#if defined(__SWITCH__)

namespace PEMUClock {

    // 0 = power save (stock/default clocks), 1 = full speed
    // (cpu max 1785mhz + gpu 460mhz while a game is running)
    void setGameMode(int mode);

    // restore the stock clocks recorded at startup (UI / game exit)
    void restore();
}

#endif // __SWITCH__

#endif // PEMU_CLOCK_H_