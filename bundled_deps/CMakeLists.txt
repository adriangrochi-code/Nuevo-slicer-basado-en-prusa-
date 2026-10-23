add_subdirectory(admesh)
add_subdirectory(avrdude)
add_subdirectory(miniz)
add_subdirectory(glu-libtess)
add_subdirectory(agg)
add_subdirectory(libigl)
add_subdirectory(hints)
add_subdirectory(libnest2d)

add_library(semver STATIC
    semver/semver.c
    semver/semver.h
)
target_include_directories(semver PUBLIC semver)
encoding_check(semver)

add_library(qoi STATIC
    qoi/qoilib.c
    qoi/qoi.h
)
target_include_directories(qoi PUBLIC qoi)
encoding_check(qoi)

add_library(fastfloat INTERFACE)
target_include_directories(fastfloat INTERFACE fast_float)

add_library(int128 INTERFACE)
target_include_directories(int128 INTERFACE int128)

add_library(localesutils STATIC
    localesutils/LocalesUtils.cpp
    localesutils/LocalesUtils.hpp
)
target_include_directories(localesutils PUBLIC localesutils)
target_link_libraries(localesutils PRIVATE fastfloat)

add_library(ankerl INTERFACE)
target_include_directories(ankerl INTERFACE ankerl)

add_library(stb_dxt INTERFACE)
target_include_directories(stb_dxt INTERFACE stb_dxt)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE stb_image)

add_library(tcbspan INTERFACE)
target_include_directories(tcbspan INTERFACE tcbspan)

add_subdirectory(prusa_fdm_mixer)

# Tisma Slicer: Vulkan loaded at run time (volk, MIT) with the Khronos headers (Apache-2.0 OR MIT), for the structural
# analysis on the GPU (src/libtisma_fea). No Vulkan SDK nor driver is needed to build or to run.
add_library(volk STATIC
    volk/volk.c
    volk/volk.h
)
target_include_directories(volk PUBLIC volk vulkan/include)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(volk PRIVATE ${CMAKE_DL_LIBS})

# Tisma Slicer: the USB cameras of the computer (UVC webcams) for the printers without one or with a slow one.
add_subdirectory(openpnp-capture)

if (SLIC3R_GUI)
    add_subdirectory(imgui)
    add_subdirectory(hidapi)
endif ()
