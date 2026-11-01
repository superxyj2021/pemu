#include "pemu_clock.h"

#if defined(__SWITCH__)

#include <cstdio>
#include <switch.h>
#include "cross2d/platforms/switch/switch_sys.h"

using namespace c2d;

namespace {

    bool g_clock_init = false;

    void ensureInit() {
        if (g_clock_init) {
            return;
        }
        if (hosversionBefore(8, 0, 0)) {
            pcvInitialize();
        } else {
            clkrstInitialize();
        }
        // record the stock (default) clocks so restore() can go back to them
        SwitchSys::stock_cpu_clock = SwitchSys::getClock(SwitchSys::Module::Cpu);
        SwitchSys::stock_gpu_clock = SwitchSys::getClock(SwitchSys::Module::Gpu);
        SwitchSys::stock_emc_clock = SwitchSys::getClock(SwitchSys::Module::Emc);
        printf("PEMUClock::ensureInit: stock clocks: cpu=%i, gpu=%i, emc=%i\n",
               SwitchSys::stock_cpu_clock, SwitchSys::stock_gpu_clock, SwitchSys::stock_emc_clock);
        g_clock_init = true;
    }
}

namespace PEMUClock {

    void setGameMode(int mode) {
        ensureInit();
        if (mode == 1) {
            // full speed: cpu max + gpu 460mhz (GPUClock::Med)
            SwitchSys::setClock(SwitchSys::Module::Cpu, (int) SwitchSys::CPUClock::Max);
            SwitchSys::setClock(SwitchSys::Module::Gpu, (int) SwitchSys::GPUClock::Med);
            printf("PEMUClock::setGameMode: full speed: cpu=%i, gpu=%i\n",
                   SwitchSys::getClock(SwitchSys::Module::Cpu),
                   SwitchSys::getClock(SwitchSys::Module::Gpu));
        } else {
            // power save: back to stock clocks
            restore();
        }
    }

    void restore() {
        ensureInit();
        if (SwitchSys::stock_cpu_clock > 0) {
            SwitchSys::setClock(SwitchSys::Module::Cpu, SwitchSys::stock_cpu_clock);
        }
        if (SwitchSys::stock_gpu_clock > 0) {
            SwitchSys::setClock(SwitchSys::Module::Gpu, SwitchSys::stock_gpu_clock);
        }
        if (SwitchSys::stock_emc_clock > 0) {
            SwitchSys::setClock(SwitchSys::Module::Emc, SwitchSys::stock_emc_clock);
        }
        printf("PEMUClock::restore: cpu=%i, gpu=%i, emc=%i\n",
               SwitchSys::getClock(SwitchSys::Module::Cpu),
               SwitchSys::getClock(SwitchSys::Module::Gpu),
               SwitchSys::getClock(SwitchSys::Module::Emc));
    }
}

#endif // __SWITCH__