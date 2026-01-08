# Generated Cmake Pico project file
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set (ROOT ${CMAKE_CURRENT_LIST_DIR})
set (PICO_TINYUSB_PATH ${ROOT}/external/tinyusb_fork  )
#set(PICO_DEOPTIMIZED_DEBUG 1)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(VFO C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(external/pico-ssd1306)
add_subdirectory(external/glm)

add_executable(${PROJECT_NAME}
    ${ROOT}/utils/mled/mled.cpp
    ${ROOT}/utils/mosc/mosc.cpp
    ${ROOT}/utils/mi2c/mi2c.cpp
    ${ROOT}/utils/musb/musb.cpp
    ${ROOT}/utils/musb/musb_vendor.cpp
    ${ROOT}/utils/musb/musb_audio.cpp
    ${ROOT}/utils/musb/musb_midi.cpp
    ${ROOT}/utils/musb/usb_descriptors.cpp
    ${ROOT}/main.cpp
    ${ROOT}/external/si5351/si5351.c
    ${ROOT}/external/pico_zest/include/pico_zest/time/pico_profiler.h
    ${ROOT}/external/pico_zest/src/time/pico_profiler.cpp
    ${ROOT}/external/zest/src/time/timer.cpp
)

# pull in common dependencies and additional i2c hardware support
target_link_libraries(${PROJECT_NAME}
    pico_ssd1306 
    pico_stdlib 
    tinyusb_device 
    tinyusb_board
    hardware_i2c 
    hardware_adc
    pico_cyw43_arch_none
    glm::glm
)

target_include_directories(${PROJECT_NAME}
 PUBLIC 
    utils/include
    utils/include/musb #for config
    external
    external/zest/include
    external/tinyusb
    external/pico-ssd1306/include
    external/pico_zest/include
    external/si5351)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PICO_W
    GLM_ENABLE_EXPERIMENTAL
    DISABLE_PROFILER_UI
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Debug>:DEBUG>
    )

    #Hide the annoying GCC9.1 parameter change stuff
target_compile_options(${PROJECT_NAME} PUBLIC -Wno-psabi)
# add url via pico_set_program_url
# pico_enable_stdio_usb(${PROJECT_NAME} 1)
# pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
