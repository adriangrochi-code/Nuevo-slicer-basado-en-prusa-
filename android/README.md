# Tisma for Android

The phone app of Tisma Slicer: Kotlin with Jetpack Compose, and the slicing core of Tisma (the same `src/` as the
desktop program, C++) through a small JNI layer (`app/src/main/cpp/tisma_jni.cpp`). Our own code: it takes ideas from
mobile slicers, not their code.

## Build

The CI builds the APK (`.github/workflows/build_android.yml`, artifact `Tisma-android`). Locally:

1. Android SDK with NDK `27.3.13750724` and CMake `3.31.6` (`sdkmanager "ndk;27.3.13750724" "cmake;3.31.6"`),
   JDK 17 or newer.
2. Tisma's dependencies for Android (once, about an hour):
   `ANDROID_NDK_ROOT=<sdk>/ndk/27.3.13750724 android/tools/build-deps.sh <build dir> <deps dir>`
   (needs ninja, make, autoconf, automake, libtool and texinfo).
3. `cd android && ./gradlew assembleDebug -PtismaDepsDir=<deps dir>/usr/local`

Without `-PtismaDepsDir` the app is built without the core (the screen says so): enough to work on the interface.

## What it does now

Open a model (STL, 3MF, OBJ), slice it on the phone with the default settings of the core (or an INI file of a
printer, print and filament), see the print time and filament, save the G-code.
