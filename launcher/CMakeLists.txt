# SPDX-License-Identifier: GPL-3.0-or-later
# The Soulcalibur II launcher: the player's game files, optional features, controllers; starts the game.
# Built beside the game program (it runs soulcalibur2 from its own folder). Needs SDL3.
# The launcher itself is the one the recomp ports share (../recomp-common/launcher, a copy of recomp-common's;
# never changed here). What is this port's is src/description.cpp, what it tells that launcher and the check of
# the game files, and ../runtime/sc2_settings.cpp, the features and the controls the game reads too.
cmake_minimum_required(VERSION 3.20)
project(soulcalibur2_launcher C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(LAUNCHER_GAME_NAME "Soulcalibur II")
set(LAUNCHER_GAME_EXE soulcalibur2)
set(LAUNCHER_APP_ID io.github.tmcleroy.soulcalibur2)
set(LAUNCHER_HOME_ENV SOULCALIBUR2_HOME)
set(LAUNCHER_DISCLAIMER "Not affiliated with Bandai Namco.")
# (and the runtime's disc image reader, to check a disc image)
# (the Guest page builds Link's data from the player's GameCube disc: guest/, which brings the disc image reader)
set(LAUNCHER_PORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/description.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/guest_page.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/sc2_settings.cpp)
# (the launcher updates itself from the releases: each system's manifest and archive, tools/release/)
set(LAUNCHER_UPDATE_REPO tmcleroy/soulcalibur2-recomp)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION LAUNCHER_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)
include(../recomp-common/launcher/launcher.cmake)
include(../guest/guest.cmake)
target_link_libraries(launcher PRIVATE sc2_guest_lib)

# (the supported Default.xbe, as tools/build_game.py checks it)
target_compile_definitions(launcher PRIVATE
    LAUNCHER_XBE_SHA256="7ce12851850ab46766f77c612b251afe7c8bd81f879e11b4836bdf766be542c6")

# Keep settings aligned with the selected game presenter.
if(NOT WIN32 OR RECOMP_WINDOWS_SDL_GPU)
    target_compile_definitions(launcher PRIVATE RECOMP_SDL_GPU_PRESENTER=1)
endif()
