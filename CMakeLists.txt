cmake_minimum_required(VERSION 3.10)

# Set the project name
project(cix_va)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find the VA API package
find_package(PkgConfig REQUIRED)
pkg_check_modules(VA REQUIRED libva)

include_directories(ddi devices devices/v4l2 devices/cme utils/packer utils/parser utils/extractor utils/tools)
include_directories(${CMAKE_SYSROOT}/usr/share/cix/include)
include_directories(${CMAKE_SYSROOT}/usr/include/cme)
link_directories(${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu)

set(SOURCES "")

# Add subdirectory
add_subdirectory(utils)
add_subdirectory(devices)
add_subdirectory(ddi)
# message(STATUS "SOURCES: ${SOURCES}")

# Add library
add_library(cix_va_drv_video SHARED ${SOURCES})

# Link the VA API libraries
target_link_libraries(cix_va_drv_video -lcme)