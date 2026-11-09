# CMake toolchain file for cross-compiling the CIX VA-API driver for AArch64
# (Orange Pi 6 Plus / Radxa Orion O6, CIX P1 CD8180/CD8160).
#
#   cmake -DCMAKE_TOOLCHAIN_FILE=../cc.cmake -DCMAKE_SYSROOT=/path/to/arm64-rootfs ..
#
# The sysroot must contain the arm64 development files of libva (and,
# optionally, libcme / mvx-v4l2-controls.h from the CIX SDK). Without
# CMAKE_SYSROOT the multiarch packages of the build host are used
# (dpkg --add-architecture arm64 && apt install libva-dev:arm64 libdrm-dev:arm64).

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CROSS_COMPILE_PREFIX "aarch64-linux-gnu-" CACHE STRING "Cross compiler prefix")
set(CMAKE_C_COMPILER   ${CROSS_COMPILE_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE_PREFIX}g++)

if(CMAKE_SYSROOT)
    set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
    set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})
    set(ENV{PKG_CONFIG_LIBDIR}
        "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
elseif(NOT DEFINED ENV{PKG_CONFIG_LIBDIR})
    set(ENV{PKG_CONFIG_LIBDIR}
        "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
