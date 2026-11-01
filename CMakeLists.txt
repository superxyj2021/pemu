cmake_minimum_required(VERSION 3.10)
#set(CMAKE_VERBOSE_MAKEFILE ON)

# devkitpro defaults to -O2 for release builds, which leaves integer-heavy
# emulator cores on the table. Force -O3 for the whole tree on switch.
# (must be set before project(), otherwise the devkitpro -O2 default wins)
if (PLATFORM_SWITCH)
    set(CMAKE_C_FLAGS_RELEASE "-g -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DNDEBUG")

    # devkitpro's switch toolchain sets -march=armv8-a+crc+crypto -mtune=cortex-a57.
    # -mcpu=cortex-a57+crc+crypto replaces both (last-wins in gcc) with codegen
    # tuned for the Tegra X1 (Cortex-A57). Same ISA, better scheduling.
    string(APPEND CMAKE_C_FLAGS " -mcpu=cortex-a57+crc+crypto")
    string(APPEND CMAKE_CXX_FLAGS " -mcpu=cortex-a57+crc+crypto")

    # fbneo/pemu code relies on type-punning that breaks under -fstrict-aliasing
    # (-O2/-O3): the old -Og debug builds never enabled it, so the bug only
    # showed up in optimized builds (crashes / wrong UI scaling). The pfbneo
    # core already forces -fno-strict-aliasing; apply it to the whole tree
    # (ui libs, sscrap, all cores) so every target behaves identically.
    string(APPEND CMAKE_C_FLAGS " -fno-strict-aliasing")
    string(APPEND CMAKE_CXX_FLAGS " -fno-strict-aliasing")
endif ()

SET(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS 1)
SET(CMAKE_CXX_RESPONSE_FILE_LINK_FLAG "@")

# add libcross2d library. If you want to port pfba to another platform,
# you should (may) only need to port libcross2d library.
add_subdirectory(external/libcross2d)

####################
# BUILD OPTIONS
####################
set(OPTION_EMU OFF CACHE STRING "Emulator to build (pfbneo, pnes, psnes)")
set(OPTION_BUILTIN_LIBCONFIG OFF CACHE BOOL "Build libconfig from sources")
set(OPTION_MPV_PLAYER ON CACHE BOOL "Add support for video snaps (mpv)")
set(OPTION_UPDATE_CONFIG OFF CACHE BOOL "Force config file updating")
####################
# BUILD OPTIONS
####################

# linux/windows screen handling options
if (PLATFORM_LINUX OR PLATFORM_WINDOWS)
    set(OPTION_RESOLUTION ON CACHE BOOL "Add screen resolution option")
    set(OPTION_FULLSCREEN ON CACHE BOOL "Add switch to fullscreen option")
endif ()

# add sscrap (screenscrapper scrapper!) library
add_subdirectory(external/sscrap)

# required for cmake toolchain setup
project(pemu)
set(PROJECT_AUTHOR cpasjuste,superxyj@bilibili)
set(VERSION_MAJOR 7)
set(VERSION_MINOR 2)

# pemu (ui)
add_subdirectory(src/skeleton)

# cores
add_subdirectory(src/cores/pfbneo)
add_subdirectory(src/cores/pgen)
add_subdirectory(src/cores/pnes)
add_subdirectory(src/cores/psnes)
add_subdirectory(src/cores/pgba)
