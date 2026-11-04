# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(sc2_graphics_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SC2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(RECOMP_RUNTIME_DIR "${SC2_ROOT}/third_party/doa2u-re/recomp-runtime" CACHE PATH "Shared runtime headers")
enable_testing()
add_executable(sc2_graphics_inventory_test ../test_graphics_inventory.cpp
    "${SC2_ROOT}/runtime/sc2_graphics_inventory.cpp")
target_include_directories(sc2_graphics_inventory_test PRIVATE "${SC2_ROOT}/runtime" "${RECOMP_RUNTIME_DIR}")
add_test(NAME sc2-graphics-inventory COMMAND sc2_graphics_inventory_test)
find_package(SDL3 CONFIG REQUIRED)
add_executable(sc2_graphics_settings_test ../test_graphics_settings.cpp
    "${SC2_ROOT}/runtime/sc2_settings.cpp" "${SC2_ROOT}/recomp-common/settings.cpp")
target_include_directories(sc2_graphics_settings_test PRIVATE "${SC2_ROOT}/runtime")
target_compile_definitions(sc2_graphics_settings_test PRIVATE RECOMP_SDL_GPU_PRESENTER=1)
target_link_libraries(sc2_graphics_settings_test PRIVATE SDL3::SDL3)
add_test(NAME sc2-graphics-settings COMMAND sc2_graphics_settings_test)

add_executable(sc2_graphics_unsupported_settings_test ../test_graphics_settings.cpp
    "${SC2_ROOT}/runtime/sc2_settings.cpp" "${SC2_ROOT}/recomp-common/settings.cpp")
target_include_directories(sc2_graphics_unsupported_settings_test PRIVATE "${SC2_ROOT}/runtime")
target_link_libraries(sc2_graphics_unsupported_settings_test PRIVATE SDL3::SDL3)
add_test(NAME sc2-graphics-unsupported-settings COMMAND sc2_graphics_unsupported_settings_test)
