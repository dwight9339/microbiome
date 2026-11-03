# WebAssembly host: the engine core as microbiome.wasm plus a JS wrapper and a
# minimal web viewer. A separate CMake project (like platform/rp2350) because
# it cross-compiles. Needs clang and lld with the wasm32 target, nothing else:
#
#   sudo apt install clang lld            # macOS: brew install llvm lld
#   cmake -S platform/wasm -B build-wasm
#   cmake --build build-wasm
#   ctest --test-dir build-wasm           # needs Node.js >= 18
#   python3 -m http.server -d build-wasm/web 8000   # then open http://localhost:8000
#
# Pass -DMB_MBSNAP=build/mbsnap to also run the pixel-parity test against the
# native renderer.

cmake_minimum_required(VERSION 3.16)

# Cross-compile settings must precede project().
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
if(NOT CMAKE_C_COMPILER)
    find_program(MB_CLANG NAMES clang clang-20 clang-19 clang-18 clang-17 clang-16 REQUIRED)
    set(CMAKE_C_COMPILER ${MB_CLANG})
endif()
set(CMAKE_C_COMPILER_TARGET wasm32-unknown-unknown)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(microbiome_wasm C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(MB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(WEB_DIR ${CMAKE_BINARY_DIR}/web)

# The core sources are compiled straight into the module: a static archive of
# wasm objects would need llvm-ar, and this avoids depending on it.
add_executable(microbiome
    mbw.c
    libc/libc.c
    ${MB_ROOT}/core/src/pack.c
    ${MB_ROOT}/core/src/world.c
    ${MB_ROOT}/core/src/actors.c
    ${MB_ROOT}/core/src/particles.c
    ${MB_ROOT}/core/src/engine.c
    ${MB_ROOT}/core/src/render.c
)
target_include_directories(microbiome PRIVATE ${MB_ROOT}/core/include ${CMAKE_CURRENT_SOURCE_DIR}/libc)
# Same viewport limit as the desktop tools, so panoramas and big previews work.
target_compile_definitions(microbiome PRIVATE MB_MAX_VIEW_W=2048)
target_compile_options(microbiome PRIVATE
    -ffreestanding -mbulk-memory -Wall -Wextra -Wshadow -Wno-unused-parameter)
target_link_options(microbiome PRIVATE
    -nostdlib -mbulk-memory
    -Wl,--no-entry -Wl,--strip-debug
    -Wl,--initial-memory=4194304 -Wl,--stack-first -Wl,-z,stack-size=262144)
set_target_properties(microbiome PROPERTIES SUFFIX ".wasm" RUNTIME_OUTPUT_DIRECTORY ${WEB_DIR})

# JS wrapper, typings and viewer next to the module.
foreach(f js/microbiome.js js/microbiome.d.ts web/index.html)
    get_filename_component(_name ${f} NAME)
    configure_file(${f} ${WEB_DIR}/${_name} COPYONLY)
endforeach()

# The forest pack, so the viewer has something to show.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(${MB_ROOT}/cmake/packs.cmake)
mb_add_pack(forest ${MB_ROOT}/packs/forest ${WEB_DIR}/forest.mbp)
add_custom_target(packs ALL DEPENDS ${WEB_DIR}/forest.mbp)

# ---- tests -----------------------------------------------------------------
enable_testing()
find_program(NODE_EXECUTABLE node)
set(MB_MBSNAP "" CACHE FILEPATH "Native mbsnap, for the pixel-parity test")
if(NODE_EXECUTABLE)
    add_test(NAME wasm_smoke
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/smoke.mjs ${WEB_DIR})
    if(MB_MBSNAP)
        add_test(NAME wasm_parity
                 COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/parity.mjs ${WEB_DIR} ${MB_MBSNAP})
    endif()
else()
    message(STATUS "Node.js not found: skipping wasm tests")
endif()
