# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

if(NOT DEFINED PROJECT_VER OR PROJECT_VER STREQUAL "")
    message(FATAL_ERROR "PROJECT_VER is required. Build with ./build_ble.sh <version>")
endif()
message(STATUS "Building xiaozhi firmware version ${PROJECT_VER}")
project(xiaozhi)
