# Native part of the Tisma Android app: Tisma's slicing core and structural analysis (the same src/ as the desktop
# program) and the JNI layer of the app (tisma_jni.cpp, engineering.cpp). The dependencies are Tisma's deps/ built for Android (android/README.md), passed by
# app/build.gradle.kts as -DTISMA_DEPS_DIR=<destdir>/usr/local.
cmake_minimum_required(VERSION 3.22)
project(TismaAndroid C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TISMA_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
if (NOT TISMA_DEPS_DIR OR NOT EXISTS "${TISMA_DEPS_DIR}")
    message(FATAL_ERROR "TISMA_DEPS_DIR (Tisma's deps built for ${ANDROID_ABI}) not found: '${TISMA_DEPS_DIR}'")
endif ()
# The NDK toolchain only searches its sysroot and CMAKE_FIND_ROOT_PATH.
list(APPEND CMAKE_FIND_ROOT_PATH ${TISMA_DEPS_DIR})
list(APPEND CMAKE_PREFIX_PATH ${TISMA_DEPS_DIR})

# The core only: no desktop GUI, tests or precompiled headers; STEP import needs OCCT for Android (not built yet).
set(SLIC3R_GUI OFF CACHE BOOL "" FORCE)
set(SLIC3R_STATIC ON CACHE BOOL "" FORCE)
set(SLIC3R_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SLIC3R_BUILD_SANDBOXES OFF CACHE BOOL "" FORCE)
set(SLIC3R_PCH OFF CACHE BOOL "" FORCE)
set(SLIC3R_ENABLE_FORMAT_STEP OFF CACHE BOOL "" FORCE)

add_subdirectory(${TISMA_SOURCE_DIR} tisma EXCLUDE_FROM_ALL)

add_library(tisma SHARED tisma_jni.cpp engineering.cpp engineering.hpp)
target_link_libraries(tisma PRIVATE
    libslic3r
    libtisma_fea
    slic3r-arrange-wrapper
    log
)
