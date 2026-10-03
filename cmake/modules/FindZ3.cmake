# Busca Z3 cuando el sistema no instala Z3Config.cmake (p. ej. libz3-dev de Ubuntu)
# y crea el target importado z3::libz3 que espera libseqarrange.
find_package(Z3 CONFIG QUIET)
if (NOT TARGET z3::libz3)
    find_path(Z3_INCLUDE_DIR z3++.h)
    find_library(Z3_LIBRARY NAMES z3 libz3)
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(Z3 REQUIRED_VARS Z3_LIBRARY Z3_INCLUDE_DIR)
    if (Z3_FOUND)
        add_library(z3::libz3 UNKNOWN IMPORTED)
        set_target_properties(z3::libz3 PROPERTIES
            IMPORTED_LOCATION "${Z3_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Z3_INCLUDE_DIR}")
    endif ()
else ()
    set(Z3_FOUND TRUE)
endif ()
