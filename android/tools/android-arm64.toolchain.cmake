# Toolchain of Tisma's deps/ (and of the core) for the Android app: 64-bit ARM, Android 8.0 (API 26), the shared C++
# runtime of the NDK (the app ships libc++_shared.so). ANDROID_NDK_ROOT: the NDK (android/README.md).
set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM 26)
set(ANDROID_STL c++_shared)
include($ENV{ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake)
# Host triplet for the autotools packages of deps/ (GMP, MPFR).
set(TOOLCHAIN_PREFIX aarch64-linux-android)
# The NDK searches packages only under its sysroot and CMAKE_FIND_ROOT_PATH: the deps already built (TISMA_ANDROID_DEPS,
# set by build-deps.sh) must be found there too, e.g. heatshrink for LibBGCode.
if (DEFINED ENV{TISMA_ANDROID_DEPS})
    list(APPEND CMAKE_FIND_ROOT_PATH $ENV{TISMA_ANDROID_DEPS})
endif ()
