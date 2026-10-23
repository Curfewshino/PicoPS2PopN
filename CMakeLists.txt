# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico CACHE STRING "Board type")

# Finds the SDK via PICO_SDK_PATH (set by the Windows installer / extension)
include(pico_sdk_import.cmake)

project(PicoPS2 C CXX ASM)

pico_sdk_init()

add_executable(PicoPS2
    src/main.c
    src/ps2_controller.c
    src/usb_descriptors.c
)

target_include_directories(PicoPS2 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

# Output mode is chosen at power-up from the buttons held (see src/main.c).
option(PS2_DPAD_AS_HAT "Gamepad mode: send the D-pad as a hat switch instead of buttons 5-8" OFF)
if(PS2_DPAD_AS_HAT)
    target_compile_definitions(PicoPS2 PRIVATE DPAD_AS_HAT=1)
endif()

target_link_libraries(PicoPS2
    pico_stdlib
    pico_unique_id
    pico_bootrom
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_usb(PicoPS2 0)
pico_enable_stdio_uart(PicoPS2 0)

pico_set_program_name(PicoPS2 "PicoPS2")
pico_add_extra_outputs(PicoPS2)
