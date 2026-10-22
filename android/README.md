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

Open a model (STL, 3MF, OBJ), slice it on the phone, see the print time and filament, save or share the G-code.

- **Printer**: any FFF printer of Tisma's profiles (brand, model, then nozzle and variant), with the bed, firmware,
  custom G-code, machine limits and the default print profile of its manufacturer; or a custom printer typed by hand.
  The catalog (`assets/printers.json`) is made when the app is built, from `resources/profiles`, by
  `tools/printer_catalog.py` (Python 3 on the build machine; `-PtismaPython=...` to choose the interpreter).
- **Filament**: Tisma's generic PLA, PETG, ABS and FLEX, with their temperatures editable.
- **Quality**: layer height, infill, walls, supports, brim.
- **Advanced**: top and bottom solid layers, infill pattern, seam, support style, supports only from the bed,
  ironing. Each one stays as the printer profile until it is changed.
- **Engineering**: the structural analysis of the desktop program (`src/libtisma_fea`) on the phone, for STL and
  OBJ models. Tap the faces where the part is held (or keep it held by the faces on the bed) and where the loads act
  (force, direction, on a point or on the whole face); choose the material (that of the filament by default), the
  safety factor and the temperature. *Analyse* tells whether it holds, with the safety factor, the largest stress and
  displacement and a colour map on the model; *Lightest infill* finds the lowest infill density that holds and can
  set it for the slice. The analysis runs in `app/src/main/cpp/engineering.cpp` with the walls, infill and filament
  of the slice settings.
