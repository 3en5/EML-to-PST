# Locates the Extended MAPI headers (mapidefs.h, mapix.h, ...).
#
# Older Windows SDKs shipped these under Include/<ver>/um, but current SDKs do
# not (verified missing in 10.0.26100.0, the SDK installed with VS 2022 as of
# 2026-08). When the SDK does not provide them we fall back to the vendored
# copy in third_party/mapi, taken from Microsoft's own MAPI Stub Library.
# See third_party/mapi/README.md.
#
# Defines the INTERFACE target wlm2pst_mapi_headers carrying the include path.

add_library(wlm2pst_mapi_headers INTERFACE)

set(WLM2PST_MAPI_HEADERS_DIR "" CACHE PATH
    "Directory containing mapidefs.h etc. Empty = probe the SDK, then use the \
vendored copy in third_party/mapi.")

if(WLM2PST_MAPI_HEADERS_DIR)
    if(NOT EXISTS "${WLM2PST_MAPI_HEADERS_DIR}/mapidefs.h")
        message(FATAL_ERROR
            "WLM2PST_MAPI_HEADERS_DIR is set to '${WLM2PST_MAPI_HEADERS_DIR}' "
            "but mapidefs.h is not there.")
    endif()
    message(STATUS "Extended MAPI headers: ${WLM2PST_MAPI_HEADERS_DIR} (user override)")
    target_include_directories(wlm2pst_mapi_headers SYSTEM INTERFACE
        "${WLM2PST_MAPI_HEADERS_DIR}")
    return()
endif()

include(CheckIncludeFileCXX)
check_include_file_cxx("mapidefs.h" WLM2PST_MAPI_HEADERS_IN_SDK)

if(WLM2PST_MAPI_HEADERS_IN_SDK)
    message(STATUS "Extended MAPI headers: found in the platform SDK")
    return()
endif()

set(_wlm2pst_vendored_mapi "${CMAKE_SOURCE_DIR}/third_party/mapi/include")
if(NOT EXISTS "${_wlm2pst_vendored_mapi}/mapidefs.h")
    message(FATAL_ERROR
        "Extended MAPI headers not found in the platform SDK, and the vendored "
        "copy is missing from ${_wlm2pst_vendored_mapi}. See "
        "third_party/mapi/README.md, or pass -DWLM2PST_MAPI_HEADERS_DIR=<path>.")
endif()

message(STATUS "Extended MAPI headers: vendored (${_wlm2pst_vendored_mapi})")
# SYSTEM so third-party header warnings never trip the project's /W4 /WX.
target_include_directories(wlm2pst_mapi_headers SYSTEM INTERFACE
    "${_wlm2pst_vendored_mapi}")
