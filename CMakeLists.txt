cmake_minimum_required(VERSION 4.0)

# Global Settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_DEBUG_POSTFIX "-debug")
set(CMAKE_RELWITHDEBINFO_POSTFIX "-reldbg")

if (WIN32)
set(VCPKG_TARGET_TRIPLET x64-windows-static-md)
endif()

set(EXTERNAL_ROOT ${CMAKE_CURRENT_LIST_DIR}/shared/external)
set(PICO_SDR_ROOT ${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/shared/external/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "Vcpkg toolchain file")

project(PicoSDR-Root
    LANGUAGES CXX C
    VERSION 0.5.0
)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(portaudio CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_subdirectory(target/external/zest)
add_subdirectory(shared/external/remidi)
add_subdirectory(shared/external/libusb)
add_subdirectory(testbed)
