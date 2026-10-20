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
    include/tisma_fea/StressLayers.hpp
    include/tisma_fea/Ribs.hpp
    include/tisma_fea/Quality.hpp
    include/tisma_fea/Serialize.hpp
    src/CellsMesh.hpp
    src/Materials.cpp
    src/Analysis.cpp
    src/Multigrid.hpp
    src/Multigrid.cpp
    src/Gpu.hpp
    src/Gpu.cpp
    src/gpu/FeaShaders.hpp
    src/ModelSetup.cpp
    src/Optimize.cpp
    src/Structures.cpp
    src/Orientation.cpp
    src/Aero.cpp
    src/StressLayers.cpp
    src/Ribs.cpp
    src/Quality.cpp
    src/Serialize.cpp
)
target_include_directories(libtisma_fea PUBLIC include)
target_link_libraries(libtisma_fea PUBLIC libslic3r PRIVATE volk)

if (SLIC3R_BUILD_TESTS)
    find_package(Catch2 3.4 REQUIRED)

    add_executable(libtisma_fea_tests test/test_fea.cpp test/test_aero.cpp test/test_serialize.cpp)
    target_link_libraries(libtisma_fea_tests PRIVATE Catch2::Catch2WithMain libtisma_fea)
    if (WIN32)
        prusaslicer_copy_dlls(libtisma_fea_tests)
    endif ()
    add_test(NAME libtisma_fea_tests COMMAND libtisma_fea_tests ${CATCH_EXTRA_ARGS})
endif ()

# tisma-fea: the analysis of an object of a project from the command line (computing servers, see
# docs/CLOUD_ARCHITECTURE.md). Next to tisma-slicer in the build tree (with the DLLs it copies there on Windows) and
# in the packages.
add_executable(tisma-fea cli/main.cpp)
target_link_libraries(tisma-fea PRIVATE libtisma_fea)
set_target_properties(tisma-fea PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
if (WIN32)
    install(TARGETS tisma-fea RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
else ()
    target_link_libraries(tisma-fea PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    install(TARGETS tisma-fea RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif ()
