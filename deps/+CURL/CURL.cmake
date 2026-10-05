set(_curl_platform_flags 
  -DENABLE_IPV6:BOOL=ON
  -DENABLE_VERSIONED_SYMBOLS:BOOL=ON
  -DENABLE_THREADED_RESOLVER:BOOL=ON

  # -DCURL_DISABLE_LDAP:BOOL=ON
  # -DCURL_DISABLE_LDAPS:BOOL=ON
  -DENABLE_MANUAL:BOOL=OFF
  # -DCURL_DISABLE_RTSP:BOOL=ON
  # -DCURL_DISABLE_DICT:BOOL=ON
  # -DCURL_DISABLE_TELNET:BOOL=ON
  # -DCURL_DISABLE_POP3:BOOL=ON
  # -DCURL_DISABLE_IMAP:BOOL=ON
  # -DCURL_DISABLE_SMB:BOOL=ON
  # -DCURL_DISABLE_SMTP:BOOL=ON
  # -DCURL_DISABLE_GOPHER:BOOL=ON
  -DHTTP_ONLY=ON

  -DCURL_USE_GSSAPI:BOOL=OFF
  -DCURL_USE_LIBSSH2:BOOL=OFF
  -DUSE_RTMP:BOOL=OFF
  -DUSE_NGHTTP2:BOOL=OFF
  -DUSE_MBEDTLS:BOOL=OFF
  # Optional features auto-detected by curl 8 that are not vendored (as in PrusaSlicer master):
  -DUSE_LIBIDN2:BOOL=OFF
  -DCURL_ZSTD:BOOL=OFF
  -DCURL_USE_LIBPSL:BOOL=OFF
  -DCURL_BROTLI:BOOL=OFF
)

if (WIN32)
  set(_curl_platform_flags ${_curl_platform_flags} -DCURL_USE_SCHANNEL=ON)
# macOS uses the curl of the system (SYSTEM_PROVIDED_PACKAGES in deps/CMakeLists.txt); SecureTransport was
# removed from curl 8.
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_curl_platform_flags 

    ${_curl_platform_flags}

    -DCURL_USE_OPENSSL:BOOL=ON

    -DCURL_CA_PATH:STRING=none
    -DCURL_CA_BUNDLE:STRING=none
    -DCURL_CA_FALLBACK:BOOL=ON
  )
endif ()

set(_patch_command "")
if (UNIX AND NOT APPLE)
  # On non-apple UNIX platforms, finding the location of OpenSSL certificates is necessary at runtime, as there is no standard location usable across platforms.
  # The OPENSSL_CERT_OVERRIDE flag is understood by PrusaSlicer and will trigger the search of certificates at initial application launch. 
  # Then ask the user for consent about the correctness of the found location.
  # CURL::libcurl is an ALIAS of CURL::libcurl_static since curl 8, and the template was renamed.
  set (_patch_command echo set_target_properties(CURL::libcurl_static PROPERTIES INTERFACE_COMPILE_DEFINITIONS OPENSSL_CERT_OVERRIDE) >> CMake/curl-config.in.cmake)
endif ()

add_cmake_project(CURL
  URL                 https://github.com/curl/curl/releases/download/curl-8_21_0/curl-8.21.0.zip
  URL_HASH            SHA256=a99651d2b9ee0bf858c590078b1b0f989c187b07009e88bf94c0ec614be1bc7d
  PATCH_COMMAND       "${_patch_command}"
  CMAKE_ARGS
    -DBUILD_TESTING:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    ${_curl_platform_flags}
)

set(DEP_CURL_DEPENDS ZLIB)
if (UNIX AND NOT APPLE)
  list(APPEND DEP_CURL_DEPENDS OpenSSL)
endif ()

