# RP2350 firmware (Raspberry Pi Pico 2 + ST7789/ILI9341 SPI LCD).
#
#   export PICO_SDK_PATH=/path/to/pico-sdk        # 2.1 or newer
#   cmake -S platform/rp2350 -B build-rp2350 [-DMB_PACK=forest] [-DMB_RTC_DS3231=ON] [-DLCD_SPI_HZ=75000000]
#                [-DMB_BOARD_CONFIG=boards/ili9341_32.h]
#   cmake --build build-rp2350
#   -> build-rp2350/microbiome.uf2 (hold BOOTSEL, plug in, copy it over)
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "Pico SDK board")
include(pico_sdk_import.cmake)
project(microbiome_rp2350 C CXX ASM)
pico_sdk_init()

set(MB_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(MB_PACK forest CACHE STRING "Pack (directory under packs/) to embed")
option(MB_RTC_DS3231 "Read wall-clock time from a DS3231 on I2C" OFF)
set(LCD_SPI_HZ "" CACHE STRING "LCD SPI clock in Hz (empty = board_config.h default)")
set(MB_BOARD_CONFIG "" CACHE STRING "Board override header, relative to platform/rp2350 or absolute (e.g. boards/ili9341_32.h)")

add_subdirectory(${MB_ROOT}/core core)
# Run the per-pixel render path from SRAM instead of XIP flash.
target_compile_definitions(mb_core PUBLIC "MB_HOT=__attribute__((section(\".time_critical.mb\")))")
target_compile_options(mb_core PRIVATE -O2)

# Use the first Python on PATH (the one with PyYAML/Pillow), not the newest
# installed: cmake_minimum_required(3.13) otherwise defaults to the latter.
set(Python3_FIND_STRATEGY LOCATION)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(${MB_ROOT}/cmake/packs.cmake)
set(PACK_OUT ${CMAKE_BINARY_DIR}/${MB_PACK}.mbp)
mb_add_pack(${MB_PACK} ${MB_ROOT}/packs/${MB_PACK} ${PACK_OUT})
add_custom_target(mb_pack DEPENDS ${PACK_OUT})

add_executable(microbiome main.c st7789.c pack_blob.S)
add_dependencies(microbiome mb_pack)
set_source_files_properties(pack_blob.S PROPERTIES OBJECT_DEPENDS ${PACK_OUT})
target_compile_definitions(microbiome PRIVATE MB_PACK_PATH="${PACK_OUT}")
target_include_directories(microbiome PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(microbiome PRIVATE mb_core pico_stdlib pico_rand hardware_spi hardware_dma)
if(MB_RTC_DS3231)
    target_sources(microbiome PRIVATE ds3231.c)
    target_compile_definitions(microbiome PRIVATE MB_RTC_DS3231=1)
    target_link_libraries(microbiome PRIVATE hardware_i2c)
endif()
if(LCD_SPI_HZ)
    target_compile_definitions(microbiome PRIVATE LCD_SPI_HZ=${LCD_SPI_HZ})
endif()
if(MB_BOARD_CONFIG)
    get_filename_component(_board ${MB_BOARD_CONFIG} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
    if(NOT EXISTS ${_board})
        message(FATAL_ERROR "MB_BOARD_CONFIG: ${_board} not found")
    endif()
    target_compile_definitions(microbiome PRIVATE MB_BOARD_CONFIG_FILE="${_board}")
endif()

pico_enable_stdio_usb(microbiome 1)
pico_enable_stdio_uart(microbiome 0)
pico_add_extra_outputs(microbiome)
