# Host build: engine core, desktop simulator (if SDL2 is found), headless
# snapshot tool, and the content packs.
#
#   cmake -B build && cmake --build build
#   ./build/mbsim build/forest.mbp
#
# The RP2350 firmware is a separate CMake project: see platform/rp2350/.
cmake_minimum_required(VERSION 3.16)
project(microbiome C)

set(CMAKE_C_STANDARD 11)
# Programs land in build/ (Visual Studio and other multi-config generators add
# a per-config subfolder: build/Release/).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)  # fopen/sscanf are fine here
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_subdirectory(core)
# Desktop tools can render whole-world panoramas; MCUs keep the default (640).
target_compile_definitions(mb_core PUBLIC MB_MAX_VIEW_W=2048)
add_subdirectory(platform/headless)

option(MB_REQUIRE_SDL "Fail configuration if SDL2 is missing (CI)" OFF)
if(MB_REQUIRE_SDL)
    find_package(SDL2 REQUIRED)
else()
    find_package(SDL2 QUIET)
endif()
if(SDL2_FOUND)
    add_subdirectory(platform/sdl)
else()
    message(STATUS "SDL2 not found: skipping the desktop simulator (mbsim)")
endif()

# ---- packs ---------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(cmake/packs.cmake)
mb_add_pack(forest ${CMAKE_SOURCE_DIR}/packs/forest ${CMAKE_BINARY_DIR}/forest.mbp)
add_custom_target(packs ALL DEPENDS ${CMAKE_BINARY_DIR}/forest.mbp)

# ---- tests -----------------------------------------------------------------
enable_testing()
add_test(NAME pack_loads
         COMMAND mbsnap ${CMAKE_BINARY_DIR}/forest.mbp - --info --sim 600)
add_test(NAME render_night_snow
         COMMAND mbsnap ${CMAKE_BINARY_DIR}/forest.mbp ${CMAKE_BINARY_DIR}/test-night.png
                 --time 22:00 --set weather=precip --set season=winter)
add_test(NAME render_rgb565_be
         COMMAND mbsnap ${CMAKE_BINARY_DIR}/forest.mbp ${CMAKE_BINARY_DIR}/test-565.png --rgb565 --time 07:00)
add_test(NAME render_odd_display
         COMMAND mbsnap ${CMAKE_BINARY_DIR}/forest.mbp ${CMAKE_BINARY_DIR}/test-480.png --size 480x480)
if(TARGET mbsim)
    # Runs the real simulator loop for 2 s without a display (also proves
    # SDL2 is found at runtime, e.g. SDL2.dll next to mbsim.exe on Windows).
    add_test(NAME simulator_smoke COMMAND mbsim ${CMAKE_BINARY_DIR}/forest.mbp --exit-after 2)
    set_tests_properties(simulator_smoke PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()
