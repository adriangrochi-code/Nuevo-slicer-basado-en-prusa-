
include(ProcessorCount)
ProcessorCount(NPROC)

set(_conf_cmd "./config")
set(_cross_arch "")
set(_cross_comp_prefix_line "")
if (CMAKE_CROSSCOMPILING)
    set(_conf_cmd "./Configure")
    set(_cross_comp_prefix_line "--cross-compile-prefix=${TOOLCHAIN_PREFIX}-")

    if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
        set(_cross_arch "linux-aarch64")
    elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armhf") # For raspbian
        # TODO: verify
        set(_cross_arch "linux-armv4")
    endif ()
endif ()

ExternalProject_Add(dep_OpenSSL
    EXCLUDE_FROM_ALL ON
    URL "https://github.com/openssl/openssl/releases/download/openssl-4.0.1/openssl-4.0.1.tar.gz"
    URL_HASH SHA256=2db3f3a0d6ea4b59e1f094ace2c8cd536dffb87cdc39084c5afa1e6f7f37dd09
    DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/OpenSSL
    BUILD_IN_SOURCE ON
    CONFIGURE_COMMAND ${_conf_cmd} ${_cross_arch}
        "--prefix=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}"
        ${_cross_comp_prefix_line}
        "--libdir=lib"
        no-shared
        -Wa,--noexecstack
    BUILD_COMMAND make depend && make "-j${NPROC}"
    INSTALL_COMMAND make install_sw
)