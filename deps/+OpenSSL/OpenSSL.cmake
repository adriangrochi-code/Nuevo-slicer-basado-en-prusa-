
include(ProcessorCount)
ProcessorCount(NPROC)

set(_conf_cmd "./config")
set(_cross_arch "")
set(_cross_comp_prefix_line "")
set(_extra_flags -Wa,--noexecstack)
if (APPLE)
    # Tisma: curl 8 has no macOS native TLS any more (SecureTransport was removed), it uses this OpenSSL. The
    # architecture is the one asked (x86_64 is cross compiled on Apple silicon); the targets are those of
    # Configurations/10-main.conf.
    set(_conf_cmd "./Configure")
    if (CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(_cross_arch "darwin64-x86_64")
    else ()
        set(_cross_arch "darwin64-arm64")
    endif ()
    # The Apple assembler does not know --noexecstack.
    set(_extra_flags "")
    if (DEP_OSX_TARGET)
        list(APPEND _extra_flags "-mmacosx-version-min=${DEP_OSX_TARGET}")
    endif ()
elseif (CMAKE_CROSSCOMPILING)
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
        ${_extra_flags}
    BUILD_COMMAND make depend && make "-j${NPROC}"
    INSTALL_COMMAND make install_sw
)