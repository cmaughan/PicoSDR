message(STATUS " CMakeLists: TestBed")

option(BUILD_TESTS "Build Tests" ON)
option(TESTBED_LIBRARY_ONLY "Only build library" OFF)

project(TestBed
    LANGUAGES CXX C
    VERSION 0.5.0
)

include(cmake/utils.cmake)

set(TESTBED_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(M3RDPARTY_DIR ${CMAKE_CURRENT_LIST_DIR}/m3rdparty)

set(TESTBED_INCLUDE_DIR include/)

configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake/config_testbed_app.h.cmake ${CMAKE_BINARY_DIR}/config_testbed_app.h)

include(cmake/all.cmake)
add_subdirectory(src)
add_subdirectory(app)

