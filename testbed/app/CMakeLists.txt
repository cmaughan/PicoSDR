project(TestBed_App VERSION 0.1.0.0)

set(APP_NAME TestBed_App)

# Set additional project information
set(COMPANY "TestBed")
set(COPYRIGHT "Copyright (c) 2019-2026 Chris Maughan. All rights reserved.")
set(IDENTIFIER "com.testbed")

set(APP_ROOT ${CMAKE_CURRENT_LIST_DIR})

list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/testbed_common.cmake)

find_package(Vulkan REQUIRED)
find_package(tinyfiledialogs CONFIG REQUIRED)


set(TESTBED_APP_SOURCE
    ${APP_ROOT}/main.cpp
    ${APP_ROOT}/testbed.cpp
    ${APP_ROOT}/radio.h
    ${APP_ROOT}/radio_settings.h
    ${APP_ROOT}/radio_settings.cpp
    ${APP_ROOT}/radio.cpp
    ${APP_ROOT}/draw_midi.cpp
    ${APP_ROOT}/draw_analysis.cpp
    ${APP_ROOT}/testbed.h
    ${APP_ROOT}/CMakeLists.txt
    )

set(RESOURCE_FOLDER ${CMAKE_CURRENT_LIST_DIR}/res)
add_project_meta(META_FILES_TO_INCLUDE)

add_executable (${APP_NAME}
    #${OS_BUNDLE}
    ${TESTBED_APP_SOURCE}
    ${META_FILES_TO_INCLUDE}
    )

target_include_directories(${APP_NAME}
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_BINARY_DIR}
    ${PICO_SDR_ROOT}
    ${PICO_SDR_ROOT}/shared/external/libusb

    ${PICO_SDR_ROOT}/target/external/pico_zest/include
    ${PICO_SDR_ROOT}/target/external/zest/include
    )

target_link_libraries (${APP_NAME}
PRIVATE
    Zest::Zest
    Zing::Zing
    usb-1.0
    Vulkan::Vulkan
    tinyfiledialogs::tinyfiledialogs
    ${PLATFORM_LINKLIBS}
    )

target_precompile_headers(${APP_NAME}
  PRIVATE
    ${APP_ROOT}/pch.h
)
target_compile_definitions(${APP_NAME}
    PUBLIC
    NO_LIBSNDFILE
    _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING)

if (WIN32)
    copy_existing_files(${PROJECT_NAME} "${RESOURCE_DEPLOY_FILES}" ${CMAKE_CURRENT_BINARY_DIR}/$(Configuration) )
endif()

source_group ("Source" FILES ${TESTBED_APP_SOURCE})
source_group ("SoundPipe" FILES ${SOUNDPIPE_SOURCE})

