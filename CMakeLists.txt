cmake_minimum_required(VERSION 3.13)

# Set the project name
project(cix_va VERSION 1.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CheckCXXSourceCompiles)

# Default to an optimized build: without a build type CMake used no -O flag
# at all, i.e. an unoptimized driver.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Only __vaDriverInit_* must be exported: every other symbol of a dlopen()ed
# driver can otherwise be interposed by (or clash with) the host application.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(CIX_VAAPI_WERROR "Treat compiler warnings as errors" OFF)
set(CIX_VAAPI_CME "AUTO" CACHE STRING "Video processing through libcme: AUTO, ON or OFF")
set_property(CACHE CIX_VAAPI_CME PROPERTY STRINGS AUTO ON OFF)
set(CIX_VAAPI_MVX_INCLUDE_DIR "${CMAKE_SYSROOT}/usr/share/cix/include"
    CACHE PATH "Directory containing mvx-v4l2-controls.h (optional)")

# Find the VA API package
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(VA REQUIRED libva>=1.9.0)
pkg_check_modules(DRM libdrm)

# Where libva looks for drivers (e.g. /usr/lib/aarch64-linux-gnu/dri).
pkg_get_variable(VA_DRIVERS_DIR_PC libva driverdir)
if(VA_DRIVERS_DIR_PC)
    # pkg-config prefixes the variable with the sysroot when cross-compiling:
    # the install path must be the path on the target.
    foreach(_root "${CMAKE_SYSROOT}" "$ENV{PKG_CONFIG_SYSROOT_DIR}")
        if(_root)
            string(FIND "${VA_DRIVERS_DIR_PC}" "${_root}" _pos)
            if(_pos EQUAL 0)
                string(LENGTH "${_root}" _len)
                string(SUBSTRING "${VA_DRIVERS_DIR_PC}" ${_len} -1 VA_DRIVERS_DIR_PC)
            endif()
        endif()
    endforeach()
    set(CIX_VAAPI_DRIVERS_DIR_DEFAULT "${VA_DRIVERS_DIR_PC}")
else()
    set(CIX_VAAPI_DRIVERS_DIR_DEFAULT "${CMAKE_INSTALL_FULL_LIBDIR}/dri")
endif()
set(CIX_VAAPI_DRIVERS_DIR "${CIX_VAAPI_DRIVERS_DIR_DEFAULT}"
    CACHE PATH "Installation directory of the VA-API driver")

# ---------------------------------------------------------------------------
# CIX libva extensions (cix-libva2). Probed instead of the previous
# "VA_MICRO_VERSION == 1" test, which broke the build with upstream libva.
# ---------------------------------------------------------------------------
set(CMAKE_REQUIRED_INCLUDES ${VA_INCLUDE_DIRS})
check_cxx_source_compiles("
#include <va/va.h>
int main() { VABufferType t = VASequenceParameterBufferType; return (int)t; }
" CIX_VA_HAS_SEQUENCE_PARAM_BUFFER)
check_cxx_source_compiles("
#include <va/va.h>
#include <va/va_dec_av1.h>
int main() {
    VADecPictureParameterBufferAV1 p = {};
    const VADecPictureParameterBufferAV1Ext *e =
        va_get_dec_picture_parameter_buffer_av1_ext_buffer(&p);
    return e ? 1 : 0;
}
" CIX_VA_HAS_AV1_EXT)
unset(CMAKE_REQUIRED_INCLUDES)

if(CIX_VA_HAS_SEQUENCE_PARAM_BUFFER)
    add_compile_definitions(CIX_VA_HAS_SEQUENCE_PARAM_BUFFER=1)
else()
    message(STATUS "libva without CIX extensions: HEVC and AV1 decoding are disabled "
                   "(install cix-libva-dev to enable them)")
endif()
if(CIX_VA_HAS_AV1_EXT)
    add_compile_definitions(CIX_VA_HAS_AV1_EXT=1)
endif()

# ---------------------------------------------------------------------------
# libcme (CIX 2D engine) for VAEntrypointVideoProc
# ---------------------------------------------------------------------------
set(CIX_VAAPI_HAVE_CME OFF)
if(NOT CIX_VAAPI_CME STREQUAL "OFF")
    find_library(CME_LIBRARY NAMES cme
        PATHS ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu ${CMAKE_SYSROOT}/usr/lib)
    find_path(CME_SYSTEM_INCLUDE_DIR NAMES cme.h
        PATHS ${CMAKE_SYSROOT}/usr/include/cme NO_DEFAULT_PATH)
    if(CME_LIBRARY)
        set(CIX_VAAPI_HAVE_CME ON)
        message(STATUS "libcme found: ${CME_LIBRARY} (video processing enabled)")
    elseif(CIX_VAAPI_CME STREQUAL "ON")
        message(FATAL_ERROR "CIX_VAAPI_CME=ON but libcme was not found")
    else()
        message(STATUS "libcme not found: video processing (VPP) disabled")
    endif()
endif()

if(CIX_VAAPI_HAVE_CME)
    add_compile_definitions(CIX_VAAPI_HAVE_CME=1)
    # Prefer the headers matching the installed libcme.
    if(CME_SYSTEM_INCLUDE_DIR)
        include_directories(BEFORE SYSTEM ${CME_SYSTEM_INCLUDE_DIR})
    endif()
endif()

include_directories(ddi devices devices/v4l2 devices/cme utils/packer utils/parser utils/extractor utils/tools)
include_directories(${VA_INCLUDE_DIRS})
if(DRM_FOUND)
    include_directories(${DRM_INCLUDE_DIRS})
endif()
if(EXISTS "${CIX_VAAPI_MVX_INCLUDE_DIR}/mvx-v4l2-controls.h")
    message(STATUS "Using MVX UAPI header from ${CIX_VAAPI_MVX_INCLUDE_DIR}")
    include_directories(AFTER ${CIX_VAAPI_MVX_INCLUDE_DIR})
else()
    message(STATUS "mvx-v4l2-controls.h not found: using built-in definitions")
endif()

add_compile_options(-Wall -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers)
if(CIX_VAAPI_WERROR)
    add_compile_options(-Werror)
endif()

set(SOURCES "")

# Add subdirectory
add_subdirectory(utils)
add_subdirectory(devices)
add_subdirectory(ddi)

# Add library
add_library(cix_va_drv_video SHARED ${SOURCES})

# libva loads "<LIBVA_DRIVER_NAME>_drv_video.so": produce cix_drv_video.so
# directly instead of libcix_va_drv_video.so + a manual symlink.
set_target_properties(cix_va_drv_video PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cix_drv_video")

target_link_libraries(cix_va_drv_video PRIVATE Threads::Threads)
if(CIX_VAAPI_HAVE_CME)
    target_link_libraries(cix_va_drv_video PRIVATE ${CME_LIBRARY})
endif()
target_link_options(cix_va_drv_video PRIVATE -Wl,--as-needed -Wl,-z,relro -Wl,-z,now)

install(TARGETS cix_va_drv_video
    LIBRARY DESTINATION ${CIX_VAAPI_DRIVERS_DIR}
    COMPONENT runtime)

message(STATUS "VA-API driver install directory: ${CIX_VAAPI_DRIVERS_DIR}")
