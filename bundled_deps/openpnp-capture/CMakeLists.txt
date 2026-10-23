# Tisma Slicer: openpnp-capture 0.0.28 (commit 32a9bdd, https://github.com/openpnp/openpnp-capture, MIT, see
# LICENSE.txt), the USB cameras (UVC webcams) of the computer: DirectShow on Windows, AVFoundation on macOS, V4L2 on
# Linux, frames always in 24-bit RGB. Only the library is kept (no tests, no bundled libjpeg-turbo), built here as a
# static library without the -march=native of its own CMakeLists.txt (the program must run on any x86-64 / arm64).
# Changed: linux/mjpeghelper.* decode MJPEG with the libjpeg API of the libjpeg-turbo Tisma already links;
# linux/platformstream.cpp clears the frame size after the capture thread ended when a stream is closed;
# common/version.h is written here instead of from git.

add_library(openpnp_capture STATIC
    common/libmain.cpp
    common/context.cpp
    common/logging.cpp
    common/stream.cpp
)
target_include_directories(openpnp_capture PUBLIC include)
target_compile_definitions(openpnp_capture PUBLIC OPENPNPCAPTURE_STATIC)
if (CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_definitions(openpnp_capture PRIVATE __BUILDTYPE__="release")
else ()
    target_compile_definitions(openpnp_capture PRIVATE __BUILDTYPE__="debug")
endif ()

if (WIN32)
    target_compile_definitions(openpnp_capture PRIVATE __PLATFORM__="Win 64 bit" _CRT_SECURE_NO_WARNINGS)
    target_sources(openpnp_capture PRIVATE win/platformcontext.cpp win/platformstream.cpp)
    target_link_libraries(openpnp_capture PRIVATE strmiids ole32 oleaut32)
elseif (APPLE)
    target_compile_definitions(openpnp_capture PRIVATE __PLATFORM__="OSX 64 bit")
    target_sources(openpnp_capture PRIVATE mac/platformcontext.mm mac/platformstream.mm mac/uvcctrl.mm)
    target_link_libraries(openpnp_capture PRIVATE
        "-framework AVFoundation"
        "-framework Foundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Accelerate"
        "-framework IOKit"
    )
else ()
    target_compile_definitions(openpnp_capture PRIVATE __PLATFORM__="Linux 64 bit")
    target_sources(openpnp_capture PRIVATE
        linux/platformcontext.cpp
        linux/platformstream.cpp
        linux/mjpeghelper.cpp
        linux/yuvconverters.cpp
    )
    find_package(JPEG REQUIRED)
    find_package(Threads REQUIRED)
    target_include_directories(openpnp_capture PRIVATE ${JPEG_INCLUDE_DIRS})
    target_link_libraries(openpnp_capture PRIVATE ${JPEG_LIBRARIES} Threads::Threads)
endif ()
