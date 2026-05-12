# Atari ST RP2040 IKDB Emulator
# Copyright (C) 2021 Roy Hopkins
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

cmake_minimum_required(VERSION 3.13...3.27)

# Apply patches for build compatibility
execute_process(
    COMMAND ${CMAKE_SOURCE_DIR}/apply-patches.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE PATCH_RESULT
)
if(NOT PATCH_RESULT EQUAL "0")
    message(WARNING "Failed to apply patches: ${PATCH_RESULT}")
endif()

# Pull in SDK (must be before project)
set(PICO_SDK_PATH ${CMAKE_SOURCE_DIR}/pico-sdk)
set(PICO_TINYUSB_PATH ${CMAKE_SOURCE_DIR}/tinyusb)
include(pico_sdk_import.cmake)

project(atari_ikbd C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

set(SOURCES
    src/main.cpp
    src/HD6301V1ST.cpp
    src/st_key_lookup_registry.cpp
    src/st_key_lookup_hid_cz_cz.cpp
    src/st_key_lookup_hid_de_ch.cpp
    src/st_key_lookup_hid_de_de.cpp
    src/st_key_lookup_hid_dk_dk.cpp
    src/st_key_lookup_hid_es_es.cpp
    src/st_key_lookup_hid_fi_fi.cpp
    src/st_key_lookup_hid_fr_ch.cpp
    src/st_key_lookup_hid_hu_hu.cpp
    src/st_key_lookup_hid_it_it.cpp
    src/st_key_lookup_hid_nl_nl.cpp
    src/st_key_lookup_hid_no_no.cpp
    src/st_key_lookup_hid_pl_pl.cpp
    src/st_key_lookup_hid_se_se.cpp
    src/st_key_lookup_hid_en_us.cpp
    src/st_key_lookup_hid_en_uk.cpp
    src/st_key_lookup_hid_fr_fr.cpp
    src/AtariSTMouse.cpp
    src/SerialPort.cpp
    src/HidInput.cpp
    src/util.cpp
    src/UserInterface.cpp
    src/UserInterface_i18n.cpp
    src/NVSettings.cpp
    src/hid_app_host.c
    src/xinput_host.c  # Official tusb_xinput driver
    src/xinput_atari.cpp  # Xbox-to-Atari joystick mapper
    src/ps4_controller.c
    ssd1306/ssd1306.c
    6301/6301.c
    hidparser/HIDParser.c
)
set(INCLUDES
    include
)

add_executable(atari_ikbd ${SOURCES})

target_include_directories(
    atari_ikbd PRIVATE 
    6301
    ssd1306
    src
    include
    hidparser
    ${INCLUDES}
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-implicit-int")

#add_definitions(-DUNIX -DPICO -DTRACE_6301 -DPICO_DEOPTIMIZED_DEBUG=1 -DLOG=2 -DDEBUG)
#add_definitions(-DUNIX -DPICO -DTRACE_6301)
add_definitions(-DUNIX -DPICO)

target_link_libraries(atari_ikbd pico_stdlib pico_multicore hardware_i2c hardware_flash hardware_sync tinyusb_host)
pico_enable_stdio_uart(atari_ikbd 1)
pico_add_extra_outputs(atari_ikbd)
pico_set_binary_type(atari_ikbd copy_to_ram)


