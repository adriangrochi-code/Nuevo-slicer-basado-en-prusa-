# Tisma Slicer: structural analysis (phase 5 of docs/IMPLEMENTATION_ROADMAP.md).
# Kept as a separate library so that the experimental code stays isolated from libslic3r.

add_library(libtisma_fea STATIC
    include/tisma_fea/Materials.hpp
    include/tisma_fea/Analysis.hpp
    include/tisma_fea/ModelSetup.hpp
    include/tisma_fea/Optimize.hpp
    include/tisma_fea/Structures.hpp
    include/tisma_fea/Orientation.hpp
    include/tisma_fea/Aero.hpp
    src/CellsMesh.hpp
    src/Materials.cpp
    src/Analysis.cpp
    src/ModelSetup.cpp
    src/Optimize.cpp
    src/Structures.cpp
    src/Orientation.cpp
    src/Aero.cpp
)
target_include_directories(libtisma_fea PUBLIC include)
target_link_libraries(libtisma_fea PUBLIC libslic3r)

if (SLIC3R_BUILD_TESTS)
    find_package(Catch2 3.4 REQUIRED)

    add_executable(libtisma_fea_tests test/test_fea.cpp test/test_aero.cpp)
    target_link_libraries(libtisma_fea_tests PRIVATE Catch2::Catch2WithMain libtisma_fea)
    if (WIN32)
        prusaslicer_copy_dlls(libtisma_fea_tests)
    endif ()
    add_test(NAME libtisma_fea_tests COMMAND libtisma_fea_tests ${CATCH_EXTRA_ARGS})
endif ()
